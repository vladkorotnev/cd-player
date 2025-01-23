# CDP Test Suite

Test suite for CD player project.

## Track listing

+-----+-----+-------------------------------------+------------+
| Trk | Idx |  Description                        | Flags      |
+-----+-----+-------------------------------------+------------+
| 1   | 0   | Initial Pre-Gap Playback Test       |            |
| 1   | 1   | Track Playback Test                 |            |
| 1   | 2   | Index Playback Test                 |            |
| 2   | 0   | Pre-Gap Playback Test               |            |
| 2   | 1   | Track Playback Test                 |            |
| 3   | 0   | (Announcement)                      |            |
| 3   | 1   | White Noise -6dB                    |            |
| 4   | 0   | (Announcement)                      |            |
| 4   | 1   | White Noise -6dB                    | PREEMPH    |
| 5   | 0   | (Announcement)                      |            |
| 5   | 1   | Sweep 20-20k L R LR                 |            |
| 6   | 0   | (Announcement)                      |            |
| 6   | 1   | 400Hz 0dBFS                         |            |
| 7   | 0   | (Announcement)                      |            |
| 7   | 1   | 1kHz -6dBFS                         |            |
| 8   | 0   | (Announcement)                      |            |
| 8   | 1   | 16kHz -12dBFS                       |            |
| 9   | 1   | End Of Test Announcement            |            |
| ... | ... | (any music of choice for testing)   |            |
+-----+-----+-------------------------------------+------------+

## Gear required

* SUT (CDP)
* USB ADC (if using software tools)
* Spectrum analyzer
* Level meter

## Test Workflow

1. Install disc in CDP
2. Check number of tracks and duration matches
3. Press play
4. Confirm that playback starts at Trk1 Idx1
5. Press and hold REW
6. Confirm that Trk1 Idx0 is playable
7. Press and hold FFWD until end of Trk1 Idx1
8. Confirm that Trk1 Idx2 is played
9. Confirm that Trk2 Idx0 is played, followed by Trk2 Idx1
10. Stop CDP. Press Next until Trk2 and press Play. Confirm that playback starts at Trk2 Idx1
11. Continue to Trk3 Idx1. Confirm no frequency response irregularities and average level.
12. Continue to Trk4 Idx1. Confirm no frequency response irregularities and average level. Confirm DEMPH indication is enabled, if present.
13. Continue to Trk5 Idx1. Confirm acceptable crosstalk between channels on L and R part. Confirm no irregularities on LR part.
14. Continue to Trk6 Idx1. Confirm frequency and level of output signal to be 400Hz 0dB.
15. Continue to Trk7 Idx1. Confirm output frequency and level to be 1000Hz -6dB.
16. Continue to Trk8 Idx1. Confirm output frequency and level to be 16000Hz -12dB.
17. Continue to Trk9 Idx1. 
18. If any music was added on the test CD, confirm no irregularities during playback.
19. Confirm FFW, REW, Next, Prev track operations.
20. Confirm auto stop at the end of CD.
21. Remove disc from CDP.
