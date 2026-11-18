<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog;

use JsonSerializable;
use Throwable;

final class MessageTemplate
{
    public const NULL_VALUE = '(null)';

    private function __construct()
    {
    }

    /**
     * @param list<mixed> $args
     * @param array<string, string> $properties filled with the placeholder values by name
     */
    public static function render(?string $template, array $args, array &$properties): string
    {
        if ($template === null) {
            return '';
        }

        $builder = '';
        $argIndex = 0;
        $length = strlen($template);
        $index = 0;

        while ($index < $length) {
            $character = $template[$index];
            if (($character === '{' || $character === '}') && ($template[$index + 1] ?? null) === $character) {
                $builder .= $character;
                $index += 2;
                continue;
            }

            $closing = $character === '{' ? strpos($template, '}', $index + 1) : false;
            if ($closing === false || $argIndex >= count($args)) {
                $builder .= $character;
                $index++;
                continue;
            }

            $value = self::format($args[$argIndex]);
            $argIndex++;
            $builder .= $value;

            $name = self::propertyName(substr($template, $index + 1, $closing - $index - 1));
            if ($name !== '') {
                $properties[$name] = $value;
            }

            $index = $closing + 1;
        }

        return $builder;
    }

    private static function propertyName(string $placeholder): string
    {
        $name = trim((string) preg_split('/[,:]/', $placeholder, 2)[0]);

        return $name !== '' && ($name[0] === '@' || $name[0] === '$') ? substr($name, 1) : $name;
    }

    /**
     * @param mixed $value
     */
    private static function format($value): string
    {
        try {
            if ($value === null) {
                return self::NULL_VALUE;
            }
            if (is_bool($value)) {
                return $value ? 'True' : 'False';
            }
            if (is_scalar($value)) {
                return (string) $value;
            }
            if ($value instanceof Throwable) {
                return $value->getMessage();
            }
            if (is_object($value) && method_exists($value, '__toString')) {
                return (string) $value;
            }
            if (is_array($value) || $value instanceof JsonSerializable) {
                return (string) json_encode($value, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE | JSON_PARTIAL_OUTPUT_ON_ERROR);
            }

            return get_debug_type($value);
        } catch (Throwable $exception) {
            return get_debug_type($value);
        }
    }
}
