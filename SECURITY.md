# Security Policy

## Supported Versions

As long as this library is in "development preview" state (with no versions officially released), only the
latest trunk version (i.e. head revision of `main` Git branch) is supported.

## Attack Vectors

Apart from testing/development tools Frame Buffet does not offer any standalone applications. Attack vectors generally
require some malicious interaction with other components of the application it is being embedded to, in the first place,
for example:

- malicious JSON configuration data provided to the framework for parsing
- establishing connections to malicious VNC servers
- receiving malicious, specially crafted data through system APIs

## Reporting Security Issues

Since the library is not supposed to be used for anything but a development preview in its current state, as of mid
2026 you may decide to simply file a publicly visible
[issue on the official repository](https://codeberg.org/dneuge/framebuffet).

Please check this file for future updates, as the process will change as the project gets closer to a release.

From that point on, security issues should initially be reported privately to the project maintainer, allowing
reasonable time to confirm and start working on a resolution before publication to minimize impact on users
(Responsible Disclosure).

You can find an email address either in the Git history after cloning a local copy or on the website linked in the user
profile.

Additional ways to contact the project will be added once it is closer to a release aimed at end-users.

**Note that all reports made to the issue tracker are immediately visible to the public** (Full Disclosure).
