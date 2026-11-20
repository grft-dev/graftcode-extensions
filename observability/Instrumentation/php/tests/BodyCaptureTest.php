<?php

declare(strict_types=1);

namespace Graftcode\Instrumentation\Tests;

use Graftcode\Instrumentation\BodyCapture;
use Graftcode\Instrumentation\Tests\Support\Harness;
use HypertubePhpUtils\type\CommandType;
use PHPUnit\Framework\TestCase;

final class BodyCaptureTest extends TestCase
{
    public function testRendersNothingForANonCommand(): void
    {
        self::assertNull(BodyCapture::render('text', 8192));
    }

    public function testRendersScalarsAndEscapesStrings(): void
    {
        $rendered = BodyCapture::render(Harness::of(CommandType::ARRAY(), 1, 2.5, true, null, "a\"b\n"), 8192);

        self::assertSame('{"runtime":"Netcore","type":"Array","payload":[1,2.5,true,null,"a\\"b\\n"]}', $rendered['body']);
        self::assertFalse($rendered['truncated']);
    }

    public function testRedactsTheValueAfterASensitiveKey(): void
    {
        $rendered = BodyCapture::render(Harness::of(CommandType::ARRAY(), 'password', 'hunter2', 'user', 'ada'), 8192);

        self::assertSame('{"runtime":"Netcore","type":"Array","payload":["password","[REDACTED]","user","ada"]}', $rendered['body']);
    }

    public function testRendersBinaryStringsAsTheirLength(): void
    {
        $rendered = BodyCapture::render(Harness::of(CommandType::ARRAY(), "\xff\xfe\x00"), 8192);

        self::assertSame('{"runtime":"Netcore","type":"Array","payload":["[bytes:3]"]}', $rendered['body']);
    }

    public function testTruncatesALongBodyAndStaysValidJson(): void
    {
        $rendered = BodyCapture::render(Harness::of(CommandType::ARRAY(), str_repeat('x', 1000), 'next'), 256);

        self::assertTrue($rendered['truncated']);
        self::assertLessThanOrEqual(256, strlen($rendered['body']));
        self::assertStringEndsWith(',"__truncated__"]}', $rendered['body']);
        self::assertIsArray(json_decode($rendered['body'], true, 512, JSON_THROW_ON_ERROR));
    }
}
