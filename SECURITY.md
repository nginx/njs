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

The F5 Security Incident Response Team (F5 SIRT) has an email alias that makes it easy to report potential security vulnerabilities.

- If you’re an F5 customer with an active support contract, please contact [F5 Technical Support](https://www.f5.com/services/support).
- If you aren’t an F5 customer, please report any potential or current instances of security vulnerabilities with any F5 product to the F5 Security Incident Response Team at F5SIRT@f5.com

For more information visit [https://www.f5.com/services/support/report-a-vulnerability](https://www.f5.com/services/support/report-a-vulnerability)
