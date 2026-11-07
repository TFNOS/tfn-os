# Governance

TFN OS is a small project with one maintainer. This file tells who decides, and how a person becomes a reviewer.

## The roles

| Role | Who | What the role can do |
|---|---|---|
| Maintainer | Omer ([@omeriko9](https://github.com/omeriko9)) | Decides the direction, the releases, the ABI, the supported boards and the store rules. Merges the pull requests. Gives and removes the roles. Enforces the code of conduct. |
| Reviewer | the persons in `.github/CODEOWNERS` for a part of the repo | Reviews and approves the pull requests of that part. Labels and closes issues. A reviewer does not merge, and does not approve their own pull request. |
| Contributor | each person who opens an issue or a pull request | Proposes changes and ideas (see [CONTRIBUTING.md](CONTRIBUTING.md)). |

## How the maintainer decides

- The maintainer makes the last decision on each topic.
- A large change (a new board, an ABI change, a change of a contract, a permanent change of the chip) starts as an
  issue. Each person can give an opinion there. The maintainer writes the decision in the issue, with the reason.
- The questions and the "show your app" posts go into GitHub Discussions.

## How a person becomes a reviewer

1. Give good contributions to one part of the repo for some time: pull requests that pass the checks and obey
   CONTRIBUTING.md, and helpful reviews of the pull requests of other persons.
2. The maintainer asks the person. A person can also ask the maintainer in an issue or a discussion.
3. When the person agrees, the maintainer adds the person to `.github/CODEOWNERS` for that part.

The maintainer can remove the role, for example when a reviewer is not active for a long time, or after a breach of
the code of conduct.

## A change of this file

A change of the governance is a pull request on this file. Only the maintainer merges it.
