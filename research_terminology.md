# V2.4-X0 research terminology

LATENT_INTENT: continuous synthetic intended stick trajectory; only synthetic generation has exact intent.

SOURCE_REPORT: a newly emitted physical/device observation.

SOURCE_CHANGED: a source report with a quantized value different from the prior report. A report can be fresh while unchanged.

DEVICE_HELD_STATE: latest source report carried between reports.

MAPPER_REPORT: 250 Hz event processed by Adaptive Response.

DEVICE_OBSERVED_TRUTH: the held device value visible at a requested future time.

EXACT_LATENT_TRUTH: exact synthetic function value at the requested time.

LATENT_POSITION_PROXY: retrospective interpolation for real physical traces, never measured between-report truth.
