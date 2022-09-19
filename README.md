# What Is This Repo?

This is our (Althea's) branch of [open5gs](https://github.com/open5gs/open5gs). Our team is firmly committed to open-sourcing and upstreaming all our contributions to open5gs whenever possible and desired by the community; this repo only holds commits that are either (a) still under active testing/development or (b) not desired by the greater open5gs community. Every change from upstream open5gs will be explained/listed below.

## Number of Served TAC/TAI
open5gs has a limit of 16 different served TAIs hard-coded in OGS_MAX_NUM_OF_SERVED_TAI. Althea's KeyLTE architecture relies on many more than that: each edge KeyLTE router has its own TAC/TAI. We have currently set OGS_MAX_NUM_OF_SERVED_TAI to 256; this might increase again in the future.