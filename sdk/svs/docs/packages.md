# Packages and compatibility

A discovered package is one directory containing `manifest.json`, the native entry library, and declared resources. Required manifest fields are `id`, `version`, `category`, `apiMajor`, `apiMinor`, `platform`, `architecture`, `entry`, `author`, `resources`. Category is exactly `Singing Voice Synthesis`. Platform is `windows`, `linux` or `macos`; architecture is `x86`, `x64` or `arm64`. Example CMake generates these values and the platform's module suffix; do not rename the library independently of the manifest.

```json
{"id":"org.example.voice","version":"0.1.0","category":"Singing Voice Synthesis","apiMajor":1,"apiMinor":1,"platform":"windows","architecture":"x64","entry":"Voice.dll","author":"Example","resources":["avatar.svg","portrait.svg"]}
```

IDs identify plugins; voice IDs identify voices inside a plugin. Both must be nonempty and unique within their namespace. Catalog voices declare stable `id`, `name`, `version`, `defaultLanguage`, `defaultLyric`, optional `avatar` and `portrait` resource IDs. Use different resource IDs when voices have different artwork. A library entry is a filename, without path separators or traversal. Images/dictionaries remain inside the package. Hosts must bound decoding and resource reads; missing or invalid artwork must leave editing usable.

The optional ABI resource API resolves declared IDs, not filesystem paths. `svs_resource_info` supplies MIME, size and SHA-256; its strings live until close. Return the exact served bytes' hash, with lowercase hex. Release handles in the module that allocated them. LMMS verifies actual bytes against this digest; the C++ resource convenience only checks bounded reads.

Version layers are separate: ABI major 1/minor 1; value Schema version 1; project XML Schema version 1; plugin and voice versions supplied by their authors. Reject incompatible ABI majors/architectures. Minor additions append optional fields; use complete-field size checks and a fallback for absent functions. The minor 0 prefix plugin remains usable, including no resource/range/pronunciation tail. Never reorder required fields, reuse an existing ID for another meaning, change an enum's stored ID to its translated label, or require an optional family merely because the host knows it.

Retain hidden/unknown user parameters, curves and unknown project data on round-trip. A voice change may alter available controls without deleting their stored inputs. Missing plugins preserve editable data and permit compatible cached playback; they cannot synthesize edited content. Cache validity includes plugin binary/version, voice/dictionary versions, full synthesis inputs, tempo mapping and output format. Presentation settings do not affect synthesis identity.

Native plugins execute in process in this release. Normal loader/query failures are isolated to the failing package; a native crash cannot be contained by this ABI. No Qt class, STL object, exception or allocator-owned pointer may cross the C boundary without its explicit corresponding release function.
