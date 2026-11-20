<?php

declare(strict_types=1);

$root = dirname(__DIR__);
$autoload = 'C:\\dev\\TestGgPhpWebApi\\vendor\\autoload.php';
if (!is_file($autoload)) {
    fwrite(STDERR, "TestGgPhpWebApi vendor autoload not found at {$autoload}\n");
    exit(2);
}

require $autoload;

use Graftcode\Telemetry\Datadog\GraftCodeDatadogConnector;
use HypertubePhpSdk\RuntimeBridge;
use HypertubePhpUtils\connectiondata\WsConnectionData;
use OpenTelemetry\API\Trace\SpanKind;

$configPath = $argv[1] ?? $root . DIRECTORY_SEPARATOR . 'matrix-config.json';
/** @var array{hosts: array<string, array<string, string>>} $config */
$config = json_decode((string) file_get_contents($configPath), true, 512, JSON_THROW_ON_ERROR);
$hosts = $config['hosts'];

$telemetry = GraftCodeDatadogConnector::start('CrossTech.OneToOne.PhpCaller');

foreach ($hosts as $calleeKey => $callee) {
    $input = 'matrix-php-' . strtolower($calleeKey);
    $spanName = "CrossMatrix.Php->{$calleeKey}";

    try {
        $traceId = '';
        $result = '';
        $telemetry->inSpan(
            $spanName,
            static function ($span) use ($callee, $input, $calleeKey, $telemetry, &$traceId, &$result): void {
                $traceId = $span->getContext()->getTraceId();
                $telemetry->getLogger()->info(
                    'CrossMatrix Caller=Php Callee={Callee} Input={Input} TraceId={TraceId}',
                    $calleeKey,
                    $input,
                    $traceId
                );

                $factory = RuntimeBridge::webSocket(new WsConnectionData($callee['ws']));
                $runtime = ($callee['runtimeArg'] ?? '') === 'ruby' ? $factory->ruby() : $factory->php();
                $value = $runtime
                    ->getType($callee['typeName'])
                    ->invokeStaticMethod($callee['methodName'], $input)
                    ->execute()
                    ->getValue();

                $result = is_scalar($value) || $value === null ? (string) $value : json_encode($value, JSON_UNESCAPED_UNICODE);
                $telemetry->getLogger()->info('CrossMatrix after call Result={Result}', $result);
            },
            SpanKind::KIND_SERVER
        );
        $telemetry->flush();

        echo 'OK|Php|', $calleeKey, '|', $traceId, '|', str_replace('|', '/', $result), PHP_EOL;
    } catch (Throwable $exception) {
        echo 'FAIL|Php|', $calleeKey, '||', $exception::class, ': ', str_replace('|', '/', $exception->getMessage()), PHP_EOL;
    }
}

exit(0);