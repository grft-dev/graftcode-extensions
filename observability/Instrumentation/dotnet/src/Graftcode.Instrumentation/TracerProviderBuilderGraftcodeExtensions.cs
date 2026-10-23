using System;
using Graftcode.Instrumentation;
using Microsoft.Extensions.Logging;

namespace OpenTelemetry.Trace
{
    public static class TracerProviderBuilderGraftcodeExtensions
    {
        public static TracerProviderBuilder AddGraftcodeInstrumentation(
            this TracerProviderBuilder builder,
            ILogger? logger = null)
        {
            if (builder == null)
            {
                throw new ArgumentNullException(nameof(builder));
            }

            return builder
                .AddSource(GraftcodeInstrumentation.ActivitySourceName)
                .AddInstrumentation(() => new GraftcodeInstrumentation(logger));
        }
    }
}
