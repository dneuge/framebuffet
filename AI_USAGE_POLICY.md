# AI Usage Policy

*Note: Training based on any data governed by this project is subject to licensing and can easily constitute a copyright infringement as explained in the [ReadMe](README.md)'s section about licensing. This document only details the use of AI for [contributions](CONTRIBUTING.md) made to this project.*

> [!warning]
> Even if you don't consciously use AI, you may be using it implicitly or encountering artifacts throughout the Internet and your toolchain. Therefore, before making *any* contributions to this project, you must carefully read and accept the terms laid out in this project.

## What is AI?

The general terms Artificial Intelligence (AI) and Machine Learning (ML) encompass a variety of technologies and mathematical constructs that aim to make predictive, analytical and somewhat "intelligent" decisions based on mathematical models and algorithms, usually in an automated fashion.

In today's common understanding (and most relevant to this policy), Artificial Intelligence (AI) generally refers to Large Language Models (LLMs) which are multi-layer neural networks trained on massive amounts of data gathered from all across the Internet plus additional offline sources (e.g. books). LLM training is a massively expensive process which, to gather enough data to result in any viable model, is only possible by ignoring or bending license agreements of ingested materials. When training or querying/prompting an LLM, input is translated to a series of tokens which are essentially equivalent to words or fragments thereof. In simplified terms, those tokens are associated with input/output parameters to the neural network. These parametric values are adjusted/transformed many times while passing through different layers of the neural network, eventually resulting in output parameters which can be translated back to tokens and thus form human-readable words and sentences again. Training very slowly adjusts coefficients within the neural network to align towards a multi-dimensional relational/topical grouping ("understanding") of similar tokens and relations between them (embedding). While training always pairs input with expected/known output, querying/prompting (so-called inference) only provides input parameters, observing the network's open output.

That process, in general, neither forms any actual intelligence nor does it provide reliability beyond varying probabilities, depending on multiple factors which cannot be reliably controlled by either end-users nor during training. Depending on how a model has been trained and modified (e.g. quantized, tuned or adjusted), every model or variation thereof may perform wildly different. Such issues are not immediately obvious, as LLM output almost always appears plausible but may in fact be entirely wrong, misleading or "ill-advised".

Several methods exist to adjust (realign/bias) or reduce (e.g. quantize) models. For example, a 70GB model may be reduced to as low as just 10GB by quantizing it from 16 bit floating point numbers down to just single bit precision to save storage as well as requiring less memory for inference. This obviously comes with a possibly significant loss of precision, potentially disqualifying a previously well-performing model from being useful for certain tasks. Additionally, parameters such as "temperature" (simplified: randomization) severely affect a model's performance. Other methods, such as Multi-Token-Prediction (MTP), may allow inference to be executed faster but not always yield the intended results.

## Where AI is found and why it affects everyone

As of time of writing, AI generated or processed content is found all over the Internet and in development tools, often unsolicited or unmarked; for example:

- web searches (e.g. Google's "AI overview")
- websites
- community resources
- official documentation
- library source code
- IDEs/editors
- code quality tools
- operating systems

Therefore, even if you don't consciously want to use AI, you may already be affected by e.g. just using Google to search the web.

## How AI may and must not be used for this project

> [!caution]
> You may be **explicitly asked to truthfully pledge compliance with these rules**; any violation may lead to immediate and permanent removal from this project, depending on severity, due to a breach of trust and tainting it with legal issues. Also note that **these rules do not override any local or international regulations** concerning AI which you are likely bound to; compliance with those regulations is your own responsibility and out of this policy's scope.

AI must not be used to generate any contributions, neither code nor any other content (Active Authoring). However, you may use it responsibly to review contributions or search/analyze the project as long as you only use AI output as hints (Passive Authoring). All AI output must be verified manually and not be taken over directly. You should also refrain from both promoting and questioning AI within the project's community (e.g. on issue reports) unless you have been actually asked for your opinion by project maintainers.

While you may use AI as a tool supporting your search for bugs, you must verify all findings manually. You must not incorporate/forward any generated code or reports, nor suggested fixes (prohibited active authoring). Please see below for specific exceptions regarding security vulnerabilities.

Please continue reading for more details.

### Prohibited: Active Authoring

Examples for active authoring are:

- using AI-based code generators - no matter if for full [vibe coding](https://en.wikipedia.org/wiki/Vibe_coding) or just for "assistance", preparation or post-editing
  - the extent does not matter: whole files, single functions, code blocks, one-line completion, even just variable or function names must not be generated automatically
- using AI agents to automate work and actively contribute to or engage with the project
- relying solely on AI output (incl. AI-generated summaries as e.g. provided unsolicited by Google web search)
- automatically generating, interacting with or processing issue reports
- sharing output generated from AI review sessions or judging other contributions purely based on such tools
  - this includes directly copying recommended code/modifications from AI review sessions
  - AI-generated reviews may only be used as an additional input for your own, manually conducted reviews as explained below
- using LLMs to rewrite texts
  - translation tools (full text transformation) are only allowed for personal use; automated translations, e.g. of issue reports written in a foreign language, must never be copied/published or quoted (at most, a summary of what you understood, formulated in your own words, may be acceptable)
- sharing or committing any other AI-generated content (e.g. in commit messages)

None of these or similar actions/tasks is allowed unless explicitly listed in the "allowed" categories below.

### Allowed: Passive Authoring

Examples for passive authoring are:

- using AI as an additional tool to help reviewing changes, e.g. as a second stage after having completed a manual (self-)review
  - any findings from AI output must be manually verified and must not be directly copied or forwarded
  - any recommendations from AI output must not be directly applied or copied
  - in general, consider everything reported by AI just as a hint, not the ultimate truth (same as if it came from a human reviewer who also makes mistakes)
- using AI to search or analyze the existing project if not possible via traditional tooling
  - remember to manually verify the results and consider that they may be incomplete or misleading (same as casually asking a human who is only vaguely familiar with the project)
  - always prefer non-AI tooling as e.g. provided by your IDE 
- only if not available any other way: seeking general advice (not directly related to this project) via AI
  - again, any such advice must be carefully examined and verified; this is mainly useful if a developer is unsure *what* to search for or how to get pointers where to start looking for more information  
- using automated translation tools or using an LLM as a translation tool for personal use
  - only permitted if not copied or directly quoted

### Allowed: Security Issues

Special exceptions apply for finding and reporting security vulnerabilities:

- using AI-assisted analysis or AI-driven fuzzing tools to detect security issues
  - when using such tools, verify any claimed security issues before reporting them in accordance to our [security policy](SECURITY.md)
  - this may involve using an "agent" and code generation to produce an exploit for (automated) verification purposes, which *only* in this one specific case is actually permitted
  - the report *should* be written manually, describing the issue in your own words (avoiding an AI-generated issue report); only the exploit (as proof) may have been AI-generated
    - this is meant to prevent "slop" reports of issues that do not actually exist or have not been evaluated correctly - if in doubt, the project will (until further notice) also accept AI generated security reports, *but only as a last resort*; please use your own brain and skills before reporting dramatic issues that may just have been hallucinated

### Allowed: Required Toolchain, Lack of Alternatives

Additional exceptions are made where AI affects the toolchain, as there are no alternatives left which do not use/incorporate AI to at least some extent:

- using operating systems which have been developed using AI as long as active authoring by those systems is prevented
- using IDEs or editors which have been developed using AI (e.g. JetBrains IDEs such as CLion) as long as active authoring is prevented
  - recommended to avoid accidental use: disable AI-supported code completion (also single-line) and grammar checks, do not use AI agents (high chance of file contents being modified by AI)
- using compilers that have been developed using AI, as long as they don't generate output actively using AI when being invoked

### Caution: Implicit and Hidden AI

It is not always immediately apparent what has been generated by or is driven by AI. We ask you to be wary of origin and quality of any information as well as to stay on top of the functions provided by the tools you use:

- make sure to properly configure your IDE/editor so that unwanted AI contributions are prevented
- unsolicited AI-generated summaries, such as provided by Google's "AI overview" above web search results, are of particularly low quality and should be disabled, blocked or at least disregarded
- some companies, such as Microsoft, are known to wildly apply AI slop (quickly generated unverified low-quality content) to official documentation and websites; reading such material can be highly misleading and should be avoided
- many websites found through web searches for development-related topics are in large parts AI-generated and probably entirely unverified (AI slop for SEO, just drawing traffic for ad revenue) and should be avoided
- discretion is advised as the amount of AI-generated content grows daily - a website/resource that could be used last year may have turned into a massive collection of automatically generated AI slop recently

### Prohibited: Discussing AI

Discussions about AI can easily get out of hand without leading anywhere. Therefore:

- AI must not be discussed or advertised within any community resources (such as the issue tracker)
  - AI evangelism is a nuisance which will not be tolerated by this project
  - discussion of AI is only permitted in regards to this policy, in clearly designated topics/issues created by project maintainers, if necessary
  - any other discussion shall only take place through direct contact with project maintainers

### General Guidelines and Advice

- always verify AI results manually, never trust them blindly; exercise careful doubt, even if the same model or inquiry had a high success rate in earlier sessions
- only use models after careful evaluation; this includes any modifications made such as runtime parameters, different quantization levels/providers or simply a changed runtime
- know how to correctly formulate prompts to each model you use - what works for one model family may not work for the other
- make sure your prompts include all information necessary to explain the context of your request (using just an agent or orchestration tool is *not* sufficient)
  - this automatically disqualifies sloppy requests; e.g. a simple "Verify the last commit." prompt will most-likely *not* produce helpful output, you usually need to provide more information about project context, circumstances and what the model should actually take a look at (possibly including a step-by-step "how to" as well)
- double-check your prompts for any typos, spelling, grammatical, formatting or copy/paste errors; the AI model may accept your input as the ultimate truth, so any issues in the prompt will cause or amplify errors in the model's output or behaviour
- do not re-use conversations/contexts for different topics
- consider cross-checking the same inquiry using different models

### Rationale

*This section just helps to explain why the project decided on its current policy. You can skip the rationale if you're not interested.*

Originally, this project had a strict "zero AI" policy, also due to earlier models not being able to coherently/methodically follow even the simplest instructions and making really gross mistakes more often than not. However, when evaluating LLMs in 2026, *some* freely available models such as Qwen 3.6 35B A3B or DeepSeek v4 turned out to be able to perform remarkably well for certain tasks, as long as they have been provided with detailed, carefully constructed prompts. While these models still have not been significantly changed in the way their neural networks work, they seem to have been successfully constrained to produce much more logical/analytical output and thus may be able to actually assist instead of just confuse developers.

Nothing changed regarding the legal situation (e.g. no universal court rulings that would indemnify copyright violation by AI "generated" code). Furthermore, unsolicited AI content and advertisements remain a constant nuisance. Therefore, any active contributions remain strictly prohibited until further notice.

Security issues (if any should be found) must be fixed and cannot be ignored, regardless of how they are being reported. While automated AI reports are discouraged, fuzzing via AI appears to have become a standard procedure and may be a highly effective way to find such defects. This scenario cannot be prohibited and thus needs to be accepted.

Cautiously allowing AI to be used for passive authoring on discretion of individual users, e.g. for an extra review stage, seems to be less problematic: Using the correct model and prompts, results have proven to not be any worse than content found on the Internet. If used responsibly and by refraining from forwarding or copy/paste-ing AI generated content to contributions and other users (those actions would constitute active authoring) there is only a minor risk which is easily outweighed by gained benefits, such as being able to receive an additional review within minutes. This is particularly valuable in small open source projects which may only be maintained by a single developer, with no consistent team being available for human reviews (which are also of varying quality and thus not too different from AI results). Ultimately, asking other human developers for help would be preferred but may simply not be feasible or too slow.

## Why you should prefer free/open models over exclusive SaaS solutions

As explained earlier, LLMs are generally trained by intentionally ignoring or bending license and copyright terms. As stated in the [ReadMe](README.md), we ask everyone who still decides to train LLMs to play fair and at least contribute back to the communities they took from. Models which are publicly released for everyone as free downloads, e.g. through [Hugging Face](https://huggingface.co/), *do* actually attempt to play fair: Without even signing up for an account everyone can simply download models such as [Qwen 3.6 35B A3B](https://huggingface.co/Qwen/Qwen3.6-35B-A3B) which are then modified by the AI community to e.g. offer quantized versions, such as those by [Unsloth](https://huggingface.co/unsloth/Qwen3.6-35B-A3B-GGUF), which can be run on your local hardware using open source tools like [Ollama](https://github.com/ollama/ollama) - for free, without any profit being generated by any of those parties.

This is in stark contrast to companies such as so-called "OpenAI" (ChatGPT), Anthropic (Claude) or xAI (Grok) who essentially exploit and violate everyone's rights who ever published anything, e.g. as Open Source or in forum or blog posts, for profit.

If you decide to use AI, we therefore kindly ask you to be mindful and **only use free models** as well as limiting cash flow to AI companies.

In case you are unable to run those models locally, you may also use an inference provider, hosting the model for you. We just kindly ask you to choose carefully which provider you decide to go with, considering e.g. their behaviour, known "license" cash flows to model providers (should be avoided since these models need to effectively be in public domain) or whether the hosting provider had already been well-established before the AI hype began or if their whole business model is just founded on exploiting other people's work/data for profit.
