using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    internal class DeviceController : ControllerBase
    {
        [HttpGet("device/cpuUsage")]
        public Task<int> GetDeviceCPUUsage() 
        { 
            return Task.FromResult((int)LinuxResourceMonitor.Instance.GetLatestResourceInfo().CpuUsagePercent); 
        }

        [HttpGet("device/ramUsage")]
        public Task<int> GetDeviceRamUsage()
        {
            return Task.FromResult((int)LinuxResourceMonitor.Instance.GetLatestResourceInfo().UsedMemoryMB);
        }

        [HttpGet("device/diskUsage")]
        public Task<int> GetDeviceDiskUsage()
        {
            return Task.FromResult((int)LinuxResourceMonitor.Instance.GetLatestResourceInfo().RootDiskUsage.UsedPercent);
        }

        // GET: frequency, governor and load of the GPU, memory controller and NPU where the board reports them (empty elsewhere)
        [HttpGet("device/accelerators")]
        public Task<AcceleratorInfo[]> GetAccelerators()
        {
            return Task.FromResult(AcceleratorMonitor.Instance.Latest);
        }

        // GET: CPU temperature; one native call per request (not cached like the other device stats).
        [HttpGet("device/temperature")]
        public Task<int> GetDeviceTemperature()
        {
            return Task.FromResult(ManagerWrapper.Instance.GetCpuTemperature());
        }
    }
}
