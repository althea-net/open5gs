# What Is This Repo?

This is our (Althea's) branch of [open5gs](https://github.com/open5gs/open5gs). Our team is firmly committed to open-sourcing and upstreaming all our contributions to open5gs whenever possible and desired by the community; this repo only holds commits that are either (a) still under active testing/development or (b) not desired by the greater open5gs community. Every change from upstream open5gs will be explained/listed below.

## Branches
The `main` (default) branch contains our most stable version of open5gs, and is generally in active deployment by our team. `dev` and `hotfix` are sometimes used for rapid testing and deployment, but come with no guarantees w.r.t code stability or branch consistency (i.e. they could deleted).

Tagged releases (e.g. `v2.4.7`) come straight from plain vanilla `open5gs` and can be considered a "starting off point" for our modifications. The corresponding branch with `_althea` at the end (e.g. `v2.4.7_althea`) is a tightly-curated (and sometimes rebased) branch that is designed to be easy for the open5gs community to read/follow. Whereas `main` might have multiple commits for a given feature as we expand utility over time and find/catch bugs, the `_althea` branch will have just one commit for a given issue.

## Number of Served TAC/TAI
open5gs has a limit of 16 different served TAIs hard-coded in `OGS_MAX_NUM_OF_SERVED_TAI`. Althea's KeyLTE architecture relies on many more than that: each edge KeyLTE router has its own TAC/TAI. We have currently set `OGS_MAX_NUM_OF_SERVED_TAI` to 256; this might increase again in the future.

## PFCP Send_ASR Option
When configuring a node's PFCP connection to another node, you now have the option of adding `send_asr: false` to the configuration yaml. This bool (which defaults to `true`) indicates whether the node should send PFCP AssociationSetupRequests or not.

The reason we need this bool/option is to turn it off for the CPS side. To allow KeyLTE routers to be seamlessly added to the existing CPS without requiring a reboot, we have to define all 256 served TAIs in the CPS configuration. Without this option, the CPS logs are full of errors trying to send ASR messages to KeyLTE routers that don't yet exist.