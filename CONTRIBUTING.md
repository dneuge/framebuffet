# Contribution Guidelines for Frame Buffet

Before contributing code for inclusion to the main repository, please make sure that

- you have read and accept the [AI Usage Policy](AI_USAGE_POLICY.md), which also applies to you if you don't consciously use or plan to use AI
- you have read all available documentation of this project, in particular the [development notes](DEVELOPMENT.md)
- the source code you are about to commit can be released under MIT license (effective runtime license after linking can differ)
  - check with project lead before introducing code restricted by licenses
- source code is formatted "correctly", see the section below
- create a pull-request to the main repository for your code (or other file changes) to be reviewed for inclusion to the main project
- when copying (or renaming) files try to remember to first commit a plain file copy without modified content, describe what you copied (from/to) in the commit message, and only modify the contents in a later commit
  - the reason is that Git cannot mark files as copies, it can only automatically detect file copies based on content changes which requires a user-configurable (thus unpredictable) amount of content to be identical to a previously committed file
- the contribution is your own work and does not introduce copyright issues to the main repository
  - significant portions of code should not be verbatim copies from forums/knowledge databases such as StackOverflow
  - basing a contribution on documentation or other (online) sources is allowed as long as you make sure to abide by the terms of use set by those sources and cite/name the original author(s) were applicable
    - when this results in a conflict to other points in these guidelines, the affected contribution needs to be considered as potentially derivative work and treated accordingly (note that e.g. including code from an external project does not necessarily violate the "own work" rule, but special care must be taken in terms of copyright and license conformity)
    - for any significant "imported" portion of code the **origin must be documented**; attempting to contribute copied code without proper attribution and clear indication of sources and licenses is a severe copyright violation
    - documentation published by Microsoft on learn.microsoft.com is highly problematic; see the section below on how to work with it
  - **AI must not have been involved for authoring any part of your contribution except for allowed cases as detailed in the [AI Usage Policy](AI_USAGE_POLICY.md)**
- all commits are made under your full real name (no aliases, no short names) and with a working email address (it must be possible to contact you under that address for the foreseeable future)
- you understand and agree to be listed with that information permanently, unrevokable, as a contributor to the project in the source repository and, at least for larger contributions, accompanying documentation
- you understand that such information will be copied and archived permanently in uncontrollable ways due to the nature of services used to collaborate on the project, as well as source code repositories and open communication on the Internet in general
- by contributing to the project, including communication, you surrender all rights to be removed from or edited out of the project at any point after your contribution has been published (this is required because rights usually granted by laws such as DSGVO/GDPR cannot be fulfilled for technical and legal reasons)

## Code formatting

As a general rule of thumb:

- for existing files: continue in the same formatting style as previously applied to that file
- for new files: check similar code for formatting style and try to replicate it
- use spaces instead of tabs, 4 spaces per indention level
- open code blocks for conditional code, even if it is a one-liner
- do not open code blocks on new lines
- use lower-case and underscores when naming functions or variables
- when defining types:
  - function references should already be pointers, the name should be suffixed `_f`
  - other types should be suffixed `_t`
- defines should be named in upper-case
- if you have open tasks remaining in your code, mark them with a comment in the format of `// TODO: description`
  - use `FIXME` if you expect potential problems from the code
  - use `TODO` for generally open tasks that do not lead to issues
  - use `DEBUG` on code that can be removed when that level of debugging is no longer required

## Use of documentation published by Microsoft

### Why learn.microsoft.com taints the project

In a very welcome move, Microsoft has opened up a lot of technical documentation at https://learn.microsoft.com/. Unfortunately, the [terms of use](https://learn.microsoft.com/en-us/legal/termsofuse) for that website, as of January 2023, are in conflict with software development and open source in particular, so any contribution based on information solely published there is legally tainted. For example:

- the website is restricted to personal and non-commercial use
  - contributions made to the project are made to the project, so it's no longer "personal use" at that point
  - the project may get used in a commercial context, so your contribution cannot be guaranteed to be of "non-commercial use"
- you copy from the website, which is forbidden
- what you copied will be modified at some point, which is forbidden
- thus you are creating a derivative work, which is forbidden
- you reproduce, distribute, transmit and publicly display what was based off that website, which is forbidden
- (re)licensing is forbidden
- running the resulting software will "perform", which is forbidden
- the MIT license does not prevent anyone from selling what was contributed, which is forbidden

This project does not currently have any written consent by Microsoft to lift those restrictions, so contributors must take special care and guarantee not to have based any contribution on affected documentation. This extends to documentation found on other websites (such as StackOverflow) which are (hopefully by naming the source) citing from that documentation. Discussions (incl. issues/bugs) within the project should refrain from including any knowledge which can only acquired from tainted sources as it will be made difficult to remedy any legal issues arising from that. If possible, any discussion involving such tainted documentation may be edited by moderators to limit legal danger for the project.

As the website has become a commonly encountered source of information when dealing with Windows-specific implementation it is hard to completely avoid it. If you cannot find any other source, you must not make a contribution based off that information.

#### Additional complication: Microsoft promoting use of AI/LLMs

Microsoft is well-known for being very persistent on promoting use of LLMs ("Copilot") throughout all their products and services, including AI slop found on official documentation. Therefore, **all Microsoft documentation edited after 2022 must be treated as potentially tainted by "AI"** and handled with special care (and an abundance of scepticism) as also detailed in this project's [AI Usage Policy](AI_USAGE_POLICY.md).

### How to mitigate

Content published to learn.microsoft.com is based on files published on GitHub which are (for the most part at least) released under open licenses which don't suffer from the restrictions put in place by the website, see: https://github.com/orgs/MicrosoftDocs/repositories?type=all

If a contribution cannot be made without involving documentation from Microsoft:
- locate documentation on one of Microsoft's GitHub repositories
- check the license conditions imposed on that repository
- if, at any point, the license turns out to be incompatible: close the repository and forget what you have seen
- if the license is compatible: start reading the documentation on GitHub
  - watch out for any changes in licensing that might appear midway in sub-directories or single files
- if the license is still compatible at the end, you are free to base your contribution on it and discuss about the contents
  - please mention the source of information and applicable license conditions in your contribution/commit as it will need to be verified to accept the contribution

Additionally, check the commit history on the Git repository. Edits up to 2022 have *probably* still been made without AI but all later edits should be considered tainted until checked:

- check commit volume & frequency (e.g. periods of mass-edits, unreasonably large commits)
- check commit header for signs of AI (commit message, authors, committer)
- check changes for typical signs of AI (e.g. formatting, comments, inconsistencies, unusual writing style, ...)
- exercise common sense, do a quick fact-check (plausibility and cross-references through traditional web searches and other sources) before wasting time on wrong documentation (there have been incidents where wrong/non-sensical AI slop was found published to learn.microsoft.com)
- resort to older revisions if in doubt
- when documenting sources also include rationale about why the external source is likely untainted by AI/LLMs (at least not to any relevant degree)

### How to deal with information about Microsoft products/APIs found on non-Microsoft sources

Due to the high chance of being legally tainted, all information found regarding Microsoft products and APIs must be verified to originate from untainted sources. This includes research on the Internet to check what other sources can be found and how wide-spread (and how old) knowledge about that information is. It makes sense to start searching on Microsoft's GitHub repository mentioned above for complete official documentation published under a compatible license.
