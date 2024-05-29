# Contributing guidelines

The following is a set of guidelines for contributing to njs.  We do
appreciate that you are considering contributing!

## Table of contents

- [Getting started](#getting-started)
- [Ask a question](#ask-a-question)
- [Contributing](#contributing)
- [Git style guide](#git-style-guide)


## Getting started

Check out the [Getting started](README.md#getting-started-with-nginx-javascript)
and [njs examples](https://github.com/nginx/njs-examples) guides to get NGINX with
njs up and running.


## Ask a question

Please open an [issue](https://github.com/nginx/njs/issues/new) on GitHub with
the label `question`.  You can also ask a question on the NGINX mailing list,
nginx@nginx.org (subscribe [here](https://mailman.nginx.org/mailman/listinfo/nginx)).


## Contributing

### Report a bug

Please ensure the bug has not already been reported as
[issue](https://github.com/nginx/njs/issues).

If the bug is a potential security vulnerability, please report using our
[security policy](SECURITY.md).

To report a non-security bug, open an
[issue](https://github.com/nginx/njs/issues/new) on GitHub with the label
`bug`.  Be sure to include a title and clear description, as much relevant
information as possible, and a code sample or an executable test case showing
the expected behavior that doesn't occur.


### Suggest a feature or enhancement

To suggest a feature or enhancement, please create an [issue](https://github.com/nginx/njs/issues/new)
on GitHub with the label `feature` or `enhancement` using the available feature
request issue template. Please ensure the feature or enhancement has not
already been suggested.

> [!NOTE]
> If you’d like to implement a new feature, please consider creating a
> feature request issue first to start a discussion about the feature
> before implementing it.


### Open a pull request

Fork the repo, create a branch, implement your changes, add any relevant tests,
submit a PR when your changes are tested and ready for review.

Before submitting a PR, please read the NGINX code guidelines to learn more
about coding conventions and style.

- Try to make it clear why the suggested change is needed, and provide a use
  case, if possible.

- Changes should be formatted according to the code style used by njs.
  njs mostly follows the [NGINX coding style](https://nginx.org/en/docs/dev/development_guide.html#code_style),
  with some minor differences. Sometimes, there is no clear rule; in such
  cases examine how existing njs sources are formatted and mimic this style.

- Submitting changes implies granting project a permission to use it under
  an [BSD-2-Clause license](LICENSE).


## Git style guide

- Keep a clean, concise and meaningful git commit history on your branch,
  rebasing locally and squashing before submitting a PR

- In the subject line, use the past tense ("Added feature", not "Add feature");
  also, use past tense to describe past scenarios, and present tense for
  current behavior

- Limit the subject line to 67 characters, and the rest of the commit message
  to 80 characters

- Use subject line prefixes for commits that affect a specific portion of the
  code; examples include "Tests:", "HTTP:", or "Core:". See the commit history
  to get an idea of the prefixes used.

- Reference issues and PRs liberally after the subject line; if the commit
  remedies a GitHub issue, [name it](https://docs.github.com/en/issues/tracking-your-work-with-issues/linking-a-pull-request-to-an-issue) accordingly
