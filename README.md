[![Project Status: Active – The project has reached a stable, usable state and is being actively developed.](https://www.repostatus.org/badges/latest/active.svg)](https://www.repostatus.org/#active)
[![Community Support](https://badgen.net/badge/support/commercial/green?icon=awesome)](/SUPPORT.md)

![NGINX JavaScript Banner](NGINX-js-1660x332.png "NGINX JavaScript Banner")

# NGINX JavaScript
NGINX JavaScript, also known as [NJS](https://nginx.org/en/docs/njs/), is a dynamic module for [NGINX](https://nginx.org/en/download.html) that enables the extension of built-in functionality using familiar JavaScript syntax. The NJS language is a subset of JavaScript, compliant with [ES5](https://262.ecma-international.org/5.1/) (ECMAScript 5.1 [Strict Variant](https://262.ecma-international.org/5.1/#sec-4.2.2)) with some [ES6](https://262.ecma-international.org/6.0/) (ECMAScript 6) and newer extensions. See [compatibility](https://nginx.org/en/docs/njs/compatibility.html) for more details.

# Table of Contents
- [How it works](#how-it-works)
- [Downloading and installing](#downloading-and-installing)
  - [Provisioning the NGINX package repository](#provisioning-the-nginx-package-repository)
  - [Installing the NGINX JavaScript modules](#installing-the-nginx-javascipt-modules)
  - [Installed files and locations](#installed-files-and-locations)
- [Getting started with NGINX JavaScript](#getting-started-with-nginx-javascript)
  - [Verify NGINX is running](#verify-nginx-is-running)
  - [Enabling the NGINX JavaScript modules](#enabling-the-nginx-javascipt-modules)
  - [Basics of writing .js script files](#basics-of-writing-js-script-files)
  - [Reference of custom objects, methods, and properties](#reference-of-custom-objects-methods-and-properties)
  - [Example: Hello World](#hello-world)
  - [The NJS command line interface (CLI)](#the-njs-command-line-interface-cli)
- [Building from source](#building-from-source)
  - [Installing dependencies](#installing-dependencies)
  - [Cloning the NGINX JavaScript GitHub repository](#cloning-the-nginx-javascript-github-repository)
  - [Building standalone command line interface utility (optional)](#building-standalone-command-line-interface-utility-optional)
  - [Cloning the NGINX GitHub repository](#cloning-the-nginx-github-repository)
  - [Building NGINX JavaScript as a module of NGINX](#building-nginx-javascript-as-a-module-of-nginx)
- [NGINX JavaScript technical specifications](#nginx-javascript-technical-specifications)
  - [Supported distributions](#supported-distributions)
  - [Supported deployment environments](#supported-deployment-environments)
  - [Supported NGINX versions](#supported-nginx-versions)
  - [Sizing recommendations](#sizing-recommendations)
- [Asking questions, reporting issues, and contributing](#asking-questions-reporting-issues-and-contributing)
- [Change log](#change-log)
- [License](#license)

# How it works
[NGINX JavaScript](https://nginx.org/en/docs/njs/) is provided as two [dynamic modules](https://nginx.org/en/linux_packages.html#dynmodules) for NGINX ([ngx_http_js_module](https://nginx.org/en/docs/http/ngx_http_js_module.html) and [ngx_stream_js_module](https://nginx.org/en/docs/stream/ngx_stream_js_module.html)) and can be added to any supported [NGINX Open Source](https://nginx.org/en/download.html) or [NGINX Plus](https://www.f5.com/products/nginx/nginx-plus) installation without recompilation. 

The NJS module allows NGINX administrators to:
- Add complex access control and security checks before requests reach upstream servers
- Manipulate response headers
- Write flexible, asynchronous content handlers, filters, and more!

See [examples](https://github.com/nginx/njs-examples/) and our various projects developed with NJS:

#### https://github.com/nginxinc/nginx-openid-connect
Extends NGINX Plus functionality to communicate directly with OIDC-compatible Identity Providers, authenticating users and authorizing content delivered by NGINX Plus.

#### https://github.com/nginxinc/nginx-saml
Reference implementation of NGINX Plus as a service provider for SAML authentication.

#### https://github.com/nginxinc/njs-prometheus-module
Exposes Prometheus metrics endpoint directly from NGINX Plus.

> [!TIP]
> NJS can also be used with the [NGINX Unit](https://unit.nginx.org/) application server. Learn more about NGINX Unit's [Control API](https://unit.nginx.org/controlapi/) and how to [define function calls with NJS](https://unit.nginx.org/scripting/).

# Downloading and installing
Follow these steps to download and install precompiled NGINX and NGINX JavaScript Linux binaries. You may also choose to [build the module locally from source code](#building-from-source).

## Provisioning the NGINX package repository
Follow [this guide](https://nginx.org/en/linux_packages.html) to add the official NGINX package repository to your system and install NGINX Open Source. If you already have NGINX Open Source or NGINX Plus installed, skip the NGINX installation portion in the last step.

## Installing the NGINX JavaScript modules
Once the repository has been provisioned, you may install NJS by issuing the following command:

### Ubuntu or Debian based systems
```bash
sudo apt install nginx-module-njs
```

### RHEL, RedHat and its derivatives
```bash
sudo yum install nginx-module-njs
```

### Alpine or similar systems
```bash
sudo apk add nginx-module-njs@nginx
```

### SuSE, SLES or similar systems
```bash
sudo zypper install nginx-module-njs
```

> [!TIP] 
> The package repository includes an alternate module that enables debug symbols. Although not recommended for production environments, this module may be helpful when developing NJS-based configurations. To download and install the debug version of the module, replace the module name in the previous command with `nginx-module-njs-dbg`.

## Installed files and locations
The package installation scripts install two modules, supporting NGINX [`http`](https://nginx.org/en/docs/http/ngx_http_core_module.html#http) and [`stream`](https://nginx.org/en/docs/stream/ngx_stream_core_module.html#stream) contexts.

- [ngx_http_js_module](https://nginx.org/en/docs/http/ngx_http_js_module.html)

    This NJS module enables manipulation of data transmitted over HTTP.
- [ngx_stream_js_module](https://nginx.org/en/docs/stream/ngx_stream_js_module.html)

    This NJS module enables manipulation of data transmitted via stream protocols such as TCP and UDP.

By default, both modules are installed into the `/etc/nginx/modules` directory.

# Getting started with NGINX JavaScript
Usage of NJS involves enabling the module, adding JavaScript files with defined functions, and invoking exported functions in NGINX configuration files.

## Verify NGINX is running
NGINX JavaScript is a module for NGINX Open Source or NGINX Plus. If you haven't done so already, follow these steps to install [NGINX Open Source](https://docs.nginx.com/nginx/admin-guide/installing-nginx/installing-nginx-open-source/) or [NGINX Plus](https://docs.nginx.com/nginx/admin-guide/installing-nginx/installing-nginx-plus/). Once installed, ensure the NGINX instance is running and able to respond to HTTP requests.

### Starting NGINX
Issue the following command to start NGINX:

```bash
sudo nginx
```

### Verify NGINX is responding to HTTP requests
```bash
curl -I 127.0.0.1
```

You should see the following response:
```bash
HTTP/1.1 200 OK
Server: nginx/1.25.5
```

## Enabling the NGINX JavaScript modules
Once installed, either (or both) NJS module(s) must be included in the NGINX configuration file. On most systems, the NGINX configuration file is located at `/etc/nginx/nginx.conf` by default.

### Edit the NGINX configuration file

```bash
sudo vi /etc/nginx/nginx.conf
```

### Enable dynamic loading of NJS modules
Use the [load_module](https://nginx.org/en/docs/ngx_core_module.html#load_module) directive in the top-level (“main”) context to enable either (or both) module(s).

```nginx
load_module modules/ngx_http_js_module.so;
load_module modules/ngx_stream_js_module.so;
```

## Basics of writing .js script files
NJS script files are typically named with a .js extension and placed in the `/etc/nginx/njs/` directory. They are usually comprised of functions that are then exported, making them available in NGINX configuration files.

## Reference of custom objects, methods, and properties
NJS provides a collection of objects with associated methods and properties that are not part of ECMAScript definitions. See the [complete reference](https://nginx.org/en/docs/njs/reference.html) to these objects and how they can be used to further extend and customize NGINX.

## Example: Hello World
Here's a basic "Hello World" example.

### example.js
The `hello` function in this file returns an HTTP 200 OK status response code along with the string "Hello World!", followed by a line feed. The function is then exported for use in an NGINX configuration file.

Add this file to the `/etc/nginx/njs` directory:

```JavaScript
function hello(r) {
  r.return(200, "Hello world!\n");
}

export default {hello}
```

### nginx.conf
We modify our NGINX configuration (`/etc/nginx/nginx.conf`) to import the JavaScript file and execute the function under specific circumstances.

```nginx
# Load the ngx_http_js_module module
load_module modules/ngx_http_js_module.so;

events {}

http {
  # Set the path to our njs JavaScript files
  js_path "/etc/nginx/njs/";

  # Import our JavaScript file into the variable "main"
  js_import main from http/hello.js;

  server {
    listen 80;

    location / {
      # Execute the "hello" function defined in our JavaScript file on all HTTP requests
      # and respond with the contents of our function.
      js_content main.hello;
    }
  }
}
```

For a full list of njs directives, see the [ngx_http_js_module](https://nginx.org/en/docs/http/ngx_http_js_module.html) and [ngx_stream_js_module](https://nginx.org/en/docs/stream/ngx_stream_js_module.html) module documentation pages.

> [!TIP]
> A more detailed version of this and other examples can be found in the official [njs-examples repository](https://github.com/nginx/njs-examples/tree/master).

## The NJS command line interface (CLI)
NGINX JavaScript installs with a command line interface utility. The interface can be opened as an interactive shell or used to process JavaScript syntax from predefined files or standard input. Since the utility runs independently, NGINX-specific objects such as [HTTP](https://nginx.org/en/docs/njs/reference.html#http) and [Stream](https://nginx.org/en/docs/njs/reference.html#http) are not available within its runtime.

### Example usage of the interactive CLI
```JavaScript
$ njs
>> globalThis
global {
  njs: njs {
    version: '0.8.4'
  },
  global: [Circular],
  process: process {
    argv: ['/usr/bin/njs'],
    env: {
      PATH: '/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin',
      HOSTNAME: 'f777c149d4f8',
      TERM: 'xterm',
      NGINX_VERSION: '1.25.5',
      NJS_VERSION: '0.8.4',
      PKG_RELEASE: '1~buster',
      HOME: '/root'
    }
  },
  console: {
    log: [Function: native],
    dump: [Function: native],
    time: [Function: native],
    timeEnd: [Function: native]
  },
  print: [Function: native]
}
>>
```

### Example usage of the non-interactive CLI
```bash
$ echo "2**3" | njs -q
8
```

# Building from source
The following steps can be used to build NGINX JavaScript as a dynamic module to be integrated into NGINX or a standalone binary for use as a command line interface utility.

> [!IMPORTANT]
> To build the module for use with NGINX, you will also need to clone, configure and build NGINX by following the steps outlined in this document.

## Installing dependencies
Most Linux distributions will require several dependencies to be installed in order to build NGINX and NGINX JavaScript. The following instructions are specific to the `apt` package manager, widely available on most Ubuntu/Debian distributions and their derivatives.

### Installing compiler and make utility

```bash
sudo apt install gcc make
```

### Installing dependency libraries

```bash
sudo apt install libpcre3-dev zlib1g-dev libssl-dev libxml2-dev libxslt-dev
```

> [!WARNING]
> This is the minimal set of dependency libraries needed to build NGINX and NJS. Other dependencies may be required if you choose to build NGINX with additional modules. Monitor the output of the `configure` command discussed in the following sections for information on which modules may be missing.

## Cloning the NGINX JavaScript GitHub repository
Using your preferred method, clone the NGINX JavaScript repository into your development directory. See [Cloning a GitHub Repository](https://docs.github.com/en/repositories/creating-and-managing-repositories/cloning-a-repository) for additional help.

```bash
https://github.com/nginx/njs.git
```

## Building the standalone command line interface utility (Optional)
The following steps are optional and only needed if you choose to build NJS as a standalone utility.

### Install dependencies
To use the NJS interactive shell, you will need to install the libedit-dev library

```bash
sudo apt install libedit-dev
```

### Configure and build
Run the following commands from the root directory of your cloned repository:

```bash
./configure
```

Build NGINX JavaScript:
```bash
make
```

The utility should now be available at `<NJS_SRC_ROOT_DIR>/build/njs`. See [The NJS Command Line Interface (CLI)](#the-njs-command-line-interface-cli) for information on usage.

## Cloning the NGINX GitHub repository
Clone the NGINX source code repository in a directory outside of the previously cloned NJS source repository.

```bash
https://github.com/nginx/nginx.git
```

## Building NGINX JavaScript as a module of NGINX
To build NGINX JavaScript as a dynamic module, execute the following commands from the NGINX source code repository's root directory:

```bash
auto/configure --add-dynamic-module=<NJS_SRC_ROOT_DIR>/nginx
```

> [!WARNING]
> By default, this method will only build the `ngx_http_js_module` module. To use NJS with the NGINX Stream module, you'll need to enable it during the `configure` step so it builds with the NGINX binary. Doing so will automatically compile the `ngx_stream_js_module` module when NJS is added to the build. One way of accomplishing this is to alter the `configure` step to:
> ```bash
> auto/configure --with-stream --add-dynamic-module=<NJS_SRC_ROOT_DIR>/nginx
> ```

Compile the module
```bash
make
```

> [!TIP]
> To build NGINX with NGINX JavaScript embedded into a single binary, alter the `configure` step to the following:
> ```bash
> auto/configure --add-module=<NJS_SRC_ROOT_DIR>/nginx
> ```

### Install module
If built as a dynamic module(s), the NGINX JavaScript module(s) will be available in the `<NGINX_SRC_ROOT_DIR>/objs/` directory. The module(s) can then be copied to an existing NGINX installation and enabled. See [Enabling the NGINX JavaScript Modules](#enabling-the-nginx-javascipt-modules) for details.

### Install compiled NGINX and NGINX JavaScript binaries
Alternatively, you may choose to install the built NGINX and NGINX JavaScript binaries by issuing the following command:

> [!IMPORTANT]
> If built into the NGINX binary as a standard (not dynamic) module, this will be the easiest method of installation

```bash
make install
```

By default, the NGINX binary will be installed into `/usr/local/nginx/sbin/nginx`. The NGINX JavaScript module(s) will be copied to `/usr/local/nginx/modules/`.

# NGINX JavaScript technical specifications
Technical specifications for NJS are identical to those of NGINX.

## Supported distributions
See [Tested Operating Systems and Platforms](https://nginx.org/en/#tested_os_and_platforms) for a complete list of supported distributions. 

## Supported deployment environments
- Container
- Public cloud (AWS, Google Cloud Platform, Microsoft Azure)
- Virtual machine

## Supported NGINX versions
NGINX JavaScript is supported by all NGINX Open Source versions starting with nginx-1.14 and all NGINX Plus versions starting with NGINX Plus R15.

# Asking questions, reporting issues, and contributing
We encourage you to engage with us. Please see the [Contributing](CONTRIBUTING.md) guide for information on how to ask questions, report issues and contribute code.

# Change log
See our [release page](https://nginx.org/en/docs/njs/changes.html) to keep track of updates.

# License
[2-clause BSD-like license](LICENSE)

---
Additional documentation available at: https://nginx.org/en/docs/njs/

©2024 F5, Inc. All rights reserved.
https://www.f5.com/products/nginx
