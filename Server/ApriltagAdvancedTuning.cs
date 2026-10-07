namespace Server
{
    // The AprilTag detector knobs beyond threads/decimation/refine (see LumenCore's ApriltagTuning): tag family, blur, hamming limit, decision-margin
    // cutoff, pose iterations and the single-/multi-tag toggles. A null member means the detector's default, so records saved before a knob existed
    // load unchanged. Persisted as one nested object on a Sink or a PipelineProfile.
    public class ApriltagAdvancedTuning
    {
        public ApriltagFamilyKind? Family { get; set; }
        public float? QuadSigma { get; set; }
        public int? MaxHamming { get; set; }
        public float? DecisionMargin { get; set; }
        public int? PoseIterations { get; set; }
        public bool? MultiTag { get; set; }
        public bool? SingleTagPose { get; set; }

        // true when every member is the default
        [System.Text.Json.Serialization.JsonIgnore]
        public bool IsDefault => !Family.HasValue && !QuadSigma.HasValue && !MaxHamming.HasValue && !DecisionMargin.HasValue
            && !PoseIterations.HasValue && !MultiTag.HasValue && !SingleTagPose.HasValue;

        // the members set here carried onto a native tuning
        public void ApplyTo(ApriltagTuning tuning)
        {
            if (Family.HasValue) tuning.family = Family.Value;
            if (QuadSigma.HasValue) tuning.quadSigma = QuadSigma.Value;
            if (MaxHamming.HasValue) tuning.maxHamming = MaxHamming.Value;
            if (DecisionMargin.HasValue) tuning.decisionMargin = DecisionMargin.Value;
            if (PoseIterations.HasValue) tuning.poseIterations = PoseIterations.Value;
            if (MultiTag.HasValue) tuning.multiTag = MultiTag.Value;
            if (SingleTagPose.HasValue) tuning.singleTagPose = SingleTagPose.Value;
        }

        // these members where set, otherwise `other`'s
        public ApriltagAdvancedTuning MergedOver(ApriltagAdvancedTuning? other) => new()
        {
            Family = Family ?? other?.Family,
            QuadSigma = QuadSigma ?? other?.QuadSigma,
            MaxHamming = MaxHamming ?? other?.MaxHamming,
            DecisionMargin = DecisionMargin ?? other?.DecisionMargin,
            PoseIterations = PoseIterations ?? other?.PoseIterations,
            MultiTag = MultiTag ?? other?.MultiTag,
            SingleTagPose = SingleTagPose ?? other?.SingleTagPose,
        };

        public ApriltagAdvancedTuning Clone() => (ApriltagAdvancedTuning)MemberwiseClone();

        // from optional query parameters; null when none was given
        public static ApriltagAdvancedTuning? FromQuery(ApriltagFamilyKind? family, float? quadSigma, int? maxHamming, float? decisionMargin,
            int? poseIterations, bool? multiTag, bool? singleTagPose)
        {
            var tuning = new ApriltagAdvancedTuning
            {
                Family = family, QuadSigma = quadSigma, MaxHamming = maxHamming, DecisionMargin = decisionMargin,
                PoseIterations = poseIterations, MultiTag = multiTag, SingleTagPose = singleTagPose,
            };
            return tuning.IsDefault ? null : tuning;
        }
    }
}
