<?php

declare(strict_types=1);

namespace Graftcode\Instrumentation;

use HypertubePhpUtils\CommandInterface;
use HypertubePhpUtils\logging\LogsCommandResolver;
use HypertubePhpUtils\logging\LogsRedactor;
use Throwable;

final class BodyCapture
{
    private const TRUNCATION_MARKER = '"__truncated__"';
    private const MAX_DEPTH = 24;
    private const CLOSING_COST_PER_LEVEL = 18;
    private const SCALAR_RESERVE = 32;

    private string $writer = '';
    private int $budget;
    private bool $truncated = false;

    private function __construct(int $budget)
    {
        $this->budget = $budget;
    }

    /**
     * @param mixed $command
     * @return array{body: string, truncated: bool}|null
     */
    public static function render($command, int $maxCharacters): ?array
    {
        if (!$command instanceof CommandInterface) {
            return null;
        }

        $capture = new BodyCapture($maxCharacters);
        $capture->writeCommand($command, 0);

        return [
            'body' => LogsRedactor::redactBody($capture->writer),
            'truncated' => $capture->truncated,
        ];
    }

    private function writeCommand(CommandInterface $command, int $depth): bool
    {
        $this->writer .= '{"runtime":"' . LogsCommandResolver::getRuntimeName($command->getRuntimeName())
            . '","type":"' . LogsCommandResolver::getCommandTypeName($command->getCommandType())
            . '","payload":[';

        $localBudget = max(0, $this->budget - (($depth + 1) * self::CLOSING_COST_PER_LEVEL) - self::SCALAR_RESERVE);
        $payload = array_values($command->getPayload());
        $complete = true;
        $hasContent = false;

        foreach ($payload as $index => $item) {
            if (strlen($this->writer) >= $localBudget) {
                $this->truncated = true;
                $complete = false;
                break;
            }

            if ($hasContent) {
                $this->writer .= ',';
            }
            $hasContent = true;

            $sensitive = $index > 0
                && is_string($payload[$index - 1] ?? null)
                && LogsRedactor::isSensitiveKey($payload[$index - 1]);
            if (!$this->writeValue($item, $depth + 1, $localBudget, $sensitive)) {
                $complete = false;
                break;
            }
        }

        if (!$complete) {
            if ($hasContent) {
                $this->writer .= ',';
            }
            $this->writer .= self::TRUNCATION_MARKER;
        }

        $this->writer .= ']}';

        return $complete;
    }

    /**
     * @param mixed $value
     */
    private function writeValue($value, int $depth, int $localBudget, bool $forceRedact): bool
    {
        if ($value === null) {
            $this->writer .= 'null';

            return true;
        }

        if ($depth >= self::MAX_DEPTH) {
            $this->truncated = true;
            $this->writer .= self::TRUNCATION_MARKER;

            return false;
        }

        if ($value instanceof CommandInterface) {
            return $this->writeCommand($value, $depth);
        }

        if (is_string($value) && preg_match('//u', $value) !== 1) {
            $this->writer .= '"[bytes:' . strlen($value) . ']"';

            return true;
        }

        if (is_bool($value)) {
            $this->writer .= $value ? 'true' : 'false';

            return true;
        }

        if (is_int($value) || is_float($value)) {
            $this->writer .= (string) $value;

            return true;
        }

        $text = is_string($value) ? $value : self::formatValue($value);

        return $this->writeString($forceRedact ? LogsRedactor::PLACEHOLDER : LogsRedactor::redactValue($text), $localBudget);
    }

    private function writeString(?string $value, int $localBudget): bool
    {
        $this->writer .= '"';
        if ($value === null) {
            $this->writer .= '"';

            return true;
        }

        $characters = preg_split('//u', $value, -1, PREG_SPLIT_NO_EMPTY) ?: [];
        foreach ($characters as $character) {
            if (strlen($this->writer) >= $localBudget) {
                $this->truncated = true;
                $this->writer .= '"';

                return false;
            }

            $this->writer .= self::escapeCharacter($character);
        }

        $this->writer .= '"';

        return true;
    }

    private static function escapeCharacter(string $character): string
    {
        switch ($character) {
            case '"':
                return '\\"';
            case '\\':
                return '\\\\';
            case "\n":
                return '\\n';
            case "\r":
                return '\\r';
            case "\t":
                return '\\t';
        }

        return strlen($character) === 1 && ord($character) < 32 ? sprintf('\\u%04x', ord($character)) : $character;
    }

    /**
     * @param mixed $value
     */
    private static function formatValue($value): string
    {
        try {
            if (is_object($value) && method_exists($value, '__toString')) {
                return (string) $value;
            }
            if (is_array($value)) {
                return (string) json_encode($value, JSON_PARTIAL_OUTPUT_ON_ERROR | JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
            }

            return is_object($value) ? get_class($value) : (string) $value;
        } catch (Throwable $exception) {
            return '[unrenderable:' . (is_object($value) ? get_class($value) : gettype($value)) . ']';
        }
    }
}
