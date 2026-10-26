package com.graftcode.context;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

public final class RequestContext {
    private static final ThreadLocal<RequestContext> CURRENT = ThreadLocal.withInitial(RequestContext::new);

    private volatile Map<String, String> headers = Collections.emptyMap();

    private RequestContext() {
    }

    public static RequestContext current() {
        return CURRENT.get();
    }

    public Map<String, String> getHeaders() {
        return headers;
    }

    public void setHeaders(HashMap<String, String> headers) {
        this.headers = headers == null
                ? Collections.<String, String>emptyMap()
                : Collections.unmodifiableMap(new HashMap<>(headers));
    }

    public String getHeader(String name) {
        String value = headers.get(name);
        if (value != null || name == null) {
            return value;
        }

        for (Map.Entry<String, String> header : headers.entrySet()) {
            if (name.equalsIgnoreCase(header.getKey())) {
                return header.getValue();
            }
        }

        return null;
    }
}
