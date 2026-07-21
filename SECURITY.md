# Security Policy

## Latest Versions

We advise users to run or update to the most recent release of njs. Older versions may not have all enhancements and/or bug fixes applied to them.

## Special Considerations

njs does not evaluate dynamic code, especially code received from the network, in any way. The only way to evaluate such code using njs is to configure the `js_import` directive in nginx. JavaScript code is loaded once during nginx start.

In the nginx/njs threat model, JavaScript code is considered a trusted source in the same way as `nginx.conf` and site certificates. This means in practice:

- Memory disclosure and other security issues triggered by JavaScript code modification are not considered security issues, but as ordinary bugs.
- Measures should be taken to protect JavaScript code used by njs.
- If no `js_import` directives are present in `nginx.conf`, nginx is safe from JavaScript-related vulnerabilities.

## Reporting a Vulnerability

Please report any vulnerabilities
[directly to F5](https://www.f5.com/services/support/report-a-vulnerability).
