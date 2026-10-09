# Random pitch prediction seeds

Production input now chooses its initial seed from the clip's random UUID rather than the constant `1`. The seed is included in host and tensor cache identity and remains stable for replay/reload of that result. Independent backend callers without an explicit seed receive a random-device seed per synthesis instance.

A fresh pitch request chooses a new pitch-stage seed from the random request UUID and the initial seed. Only pitch model sessions are reset for that change; unaffected stages retain their seed and cache identity. Changed predicted pitch naturally invalidates downstream models whose inputs change.

Validation on 2026-10-09: native interpolation, actual context-menu actions and real two-note Furina rendering passed (5 passed, no failures). Real backend re-prediction generated different pitch feedback and 5 fresh SHA256 tensor files; repeating the same request reused them and produced identical audio. Cache replay and uncached rendering for the original request matched. An initial seed-key implementation was rejected by the tensor codec for empty inputs; it was corrected to include the seed tensor before the passing backend run.

Audition: `cache/SVS/DiffSinger/6be906c65b8370b500e7588827efd88f5b57caeee06886dc17cfe7a84960bcb6.wav`. This supersedes the fixed-seed behavior described in the earlier continuity validation record.
