# What Is This Repo?

This is our (Althea's) branch of [open5gs](https://github.com/open5gs/open5gs). Our team is firmly committed to open-sourcing and upstreaming all our contributions to open5gs whenever possible and desired by the community; this repo only holds commits that are either (a) still under active testing/development or (b) not desired by the greater open5gs community. Every change from upstream open5gs will be explained/listed below.

## Branches
The `main` (default) branch contains our most stable version of open5gs, and is generally in active deployment by our team. `dev` and `hotfix` are sometimes used for rapid testing and deployment, but come with no guarantees w.r.t code stability or branch consistency (i.e. they could deleted).

Tagged releases (e.g. `v2.4.7`) come straight from plain vanilla `open5gs` and can be considered a "starting off point" for our modifications. The corresponding branch with `_althea` at the end (e.g. `v2.4.7_althea`) is a tightly-curated (and sometimes rebased) branch that is designed to be easy for the open5gs community to read/follow. Whereas `main` might have multiple commits for a given feature as we expand utility over time and find/catch bugs, the `_althea` branch will have just one commit for a given issue.