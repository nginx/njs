# TypeScript definitions for njs

This package contains type definitions for [njs](https://github.com/nginx/njs) – NGINX JavaScript.


## Usage

Install **njs-types** from the [npm registry](https://www.npmjs.com/) into your project:

```sh
# using npm:
npm install --save-dev njs-types
# or using yarn:
yarn add --dev njs-types
```

njs-types provides three entry points with global declarations for each of njs environments:

* `njs_shell.d.ts` – njs shell
* `ngx_http_js_module.d.ts` – NGINX JS HTTP Module
* `ngx_stream_js_module.d.ts` – NGINX JS Stream Module

You can either reference them using [triple-slash directive](https://www.typescriptlang.org/docs/handbook/triple-slash-directives.html) at top of your `.ts` files (adjust `path` to point into your project’s node_modules):

```ts
/// <reference path="./node_modules/njs-types/ngx_http_js_module.d.ts" />
```

or include them using the [files](https://www.typescriptlang.org/tsconfig#files) flag in your `tsconfig.json`, for example:

```json
{
  "compilerOptions": {
    "target": "ES5",
    "module": "es2015",
    "lib": [
      "ES2015",
      "ES2016.Array.Include",
      "ES2017.Object",
      "ES2017.String"
    ],
    "outDir": "./lib",
    "downlevelIteration": true,

    "strict": true,
    "noImplicitAny": true,
    "strictNullChecks": true,
    "strictFunctionTypes": true,
    "strictBindCallApply": true,
    "strictPropertyInitialization": true,
    "noImplicitThis": true,
    "alwaysStrict": true,

    "moduleResolution": "node",

    "skipLibCheck": true,
    "forceConsistentCasingInFileNames": true,
  },
  "include": [
    "./src",
  ],
  "files": [
    "./node_modules/njs-types/ngx_http_js_module.d.ts",
  ],
}
```


## Versions

njs-types is typically being released together with njs.
Their major and minor release numbers (the first two numbers) are always aligned, but the patch version (the third number) may differ.
That's because njs-types may be updated between njs releases and in such case the patch version is incremented.

It's the same strategy as used in [DefinitelyTyped](https://github.com/DefinitelyTyped/DefinitelyTyped#how-do-definitely-typed-package-versions-relate-to-versions-of-the-corresponding-library).
The reason is that npmjs enforces [SemVer](https://semver.org/) which doesn't allow four-part version number nor provide post-release suffixes.

You can find from which commit the package was built in file `COMMITHASH` inside the published package.
It contains global revision id in the upstream repository https://hg.nginx.org/njs/ (Mercurial).
