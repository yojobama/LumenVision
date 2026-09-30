using System;
using System.Device.Gpio;
using System.Device.Gpio.Drivers;
using System.Threading;

namespace Server
{
    // vision LED modes, numbered as PhotonVision's VisionLEDMode
    public enum LedMode
    {
        Default = -1,
        Off = 0,
        On = 1,
        Blink = 2,
    }

    // Drives the physical LED line.
    public interface ILedOutput : IDisposable
    {
        void Set(bool on);
    }

    // A GPIO line opened through libgpiod.
    public sealed class GpioLedOutput : ILedOutput
    {
        private readonly GpioController controller;
        private readonly int line;
        private readonly bool activeLow;

        public GpioLedOutput(LedSettings settings)
        {
            line = settings.Line;
            activeLow = settings.ActiveLow;
            controller = new GpioController(PinNumberingScheme.Logical, new LibGpiodDriver(settings.Chip));
            controller.OpenPin(line, PinMode.Output);
        }

        public void Set(bool on)
        {
            controller.Write(line, on != activeLow ? PinValue.High : PinValue.Low);
        }

        public void Dispose() => controller.Dispose();
    }

    // Applies an LED mode to an output; Default is off, Blink toggles every 500 ms.
    public sealed class LedController : IDisposable
    {
        private static readonly TimeSpan BlinkPeriod = TimeSpan.FromMilliseconds(500);

        public static LedController Instance { get; } = new LedController(OpenConfiguredOutput());

        private readonly ILedOutput? output;
        private readonly object sync = new object();
        private readonly Timer blinkTimer;
        private bool blinkState;

        public LedController(ILedOutput? output)
        {
            this.output = output;
            blinkTimer = new Timer(_ => Toggle(), null, Timeout.Infinite, Timeout.Infinite);
            Mode = LedMode.Default;
            Apply(false);
        }

        // false when no LED is configured (or GPIO is unavailable): modes are still tracked and reported back, but nothing is driven
        public bool Available => output != null;

        public LedMode Mode { get; private set; }

        public void SetMode(LedMode mode)
        {
            lock (sync)
            {
                Mode = mode;
                blinkState = mode == LedMode.On;
                switch (mode)
                {
                    case LedMode.On:
                        blinkTimer.Change(Timeout.Infinite, Timeout.Infinite);
                        Apply(true);
                        break;
                    case LedMode.Blink:
                        blinkState = true;
                        Apply(true);
                        blinkTimer.Change(BlinkPeriod, BlinkPeriod);
                        break;
                    default:
                        blinkTimer.Change(Timeout.Infinite, Timeout.Infinite);
                        Apply(false);
                        break;
                }
            }
        }

        private void Toggle()
        {
            lock (sync)
            {
                if (Mode != LedMode.Blink) return;
                blinkState = !blinkState;
                Apply(blinkState);
            }
        }

        private void Apply(bool on)
        {
            try
            {
                output?.Set(on);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"LED output failed: {ex.Message}");
            }
        }

        private static ILedOutput? OpenConfiguredOutput()
        {
            LedSettings settings = DeviceSettings.Instance.Data.Led;
            if (!settings.Enabled) return null;
            try
            {
                return new GpioLedOutput(settings);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"LED GPIO chip {settings.Chip} line {settings.Line} unavailable: {ex.Message}");
                return null;
            }
        }

        public void Dispose()
        {
            blinkTimer.Dispose();
            Apply(false);
            output?.Dispose();
        }
    }
}
