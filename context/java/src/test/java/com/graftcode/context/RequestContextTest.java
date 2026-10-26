package com.graftcode.context;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import java.util.HashMap;
import java.util.concurrent.atomic.AtomicReference;

class RequestContextTest {

    @Test
    void headers_AreReadCaseInsensitively_AndCannotBeMutated() {
        HashMap<String, String> headers = new HashMap<>();
        headers.put("traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01");
        RequestContext.current().setHeaders(headers);
        headers.clear();

        Assertions.assertEquals("00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01", RequestContext.current().getHeader("TraceParent"));
        Assertions.assertThrows(UnsupportedOperationException.class, () -> RequestContext.current().getHeaders().put("x", "y"));
    }

    @Test
    void eachThread_HasItsOwnContext() throws InterruptedException {
        HashMap<String, String> headers = new HashMap<>();
        headers.put("traceid", "main");
        RequestContext.current().setHeaders(headers);

        AtomicReference<String> seen = new AtomicReference<>("unset");
        Thread other = new Thread(() -> seen.set(RequestContext.current().getHeader("traceid")));
        other.start();
        other.join();

        Assertions.assertNull(seen.get());
        Assertions.assertEquals("main", RequestContext.current().getHeader("traceid"));
    }

    @Test
    void nullHeaders_ClearTheContext() {
        RequestContext.current().setHeaders(null);

        Assertions.assertTrue(RequestContext.current().getHeaders().isEmpty());
    }
}
