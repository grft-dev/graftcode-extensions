<?php

declare(strict_types=1);

namespace Graftcode\Telemetry\Datadog\Tests;

use Graftcode\Telemetry\Datadog\MessageTemplate;
use PHPUnit\Framework\TestCase;

final class MessageTemplateTest extends TestCase
{
    public function testFillsPlaceholdersByPositionAndCollectsProperties(): void
    {
        $properties = [];

        $message = MessageTemplate::render('Caller after Graft call Result={Result} in {Elapsed:0.0}ms', ['ok', 12], $properties);

        self::assertSame('Caller after Graft call Result=ok in 12ms', $message);
        self::assertSame(['Result' => 'ok', 'Elapsed' => '12'], $properties);
    }

    public function testKeepsEscapedBracesAndUnfilledPlaceholders(): void
    {
        $properties = [];

        self::assertSame('{literal} value {Missing}', MessageTemplate::render('{{literal}} {Value} {Missing}', ['value'], $properties));
    }

    public function testStripsDestructuringPrefixesAndFormatsValuesLikeDotnet(): void
    {
        $properties = [];

        $message = MessageTemplate::render('{@Payload} {$Flag} {Nothing}', [['a' => 1], true, null], $properties);

        self::assertSame('{"a":1} True (null)', $message);
        self::assertSame(['Payload' => '{"a":1}', 'Flag' => 'True', 'Nothing' => '(null)'], $properties);
    }

    public function testRendersNothingForANullTemplate(): void
    {
        $properties = [];

        self::assertSame('', MessageTemplate::render(null, ['x'], $properties));
    }
}
