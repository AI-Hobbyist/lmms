# LMMS local command reference

Generated from the running command registry. Positions and lengths use native ticks unless a command says otherwise. Track and clip indices follow the current project order. Project edits support dryRun and undo; file writes and playback are outside project undo.

## agent.commandHelp

Local registry helper: commandHelp. MCP clients use tools/list for discovery.

```json
{
    "additionalProperties": false,
    "properties": {
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "name"
    ],
    "type": "object"
}
```

## agent.diffPreview

Preview project command steps without retaining changes or history.

```json
{
    "additionalProperties": false,
    "properties": {
        "commands": {
            "description": "The commands argument for this command.",
            "items": {
                "type": "object"
            },
            "maxItems": 20000,
            "type": "array"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        }
    },
    "required": [
        "commands"
    ],
    "type": "object"
}
```

## agent.getContext

Local registry helper: getContext. MCP clients use tools/list for discovery.

```json
{
    "additionalProperties": false,
    "properties": {
    },
    "required": [
    ],
    "type": "object"
}
```

## agent.listCommands

Local registry helper: listCommands. MCP clients use tools/list for discovery.

```json
{
    "additionalProperties": false,
    "properties": {
    },
    "required": [
    ],
    "type": "object"
}
```

## agent.runScript

Run bounded JSON music steps with one project undo and optional dryRun.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "script": {
            "anyOf": [
                {
                    "type": "object"
                },
                {
                    "type": "string"
                }
            ],
            "description": "The script argument for this command."
        },
        "seed": {
            "description": "The seed argument for this command.",
            "maximum": 2147483647,
            "minimum": -2147483648,
            "type": "integer"
        },
        "vars": {
            "description": "The vars argument for this command.",
            "type": "object"
        }
    },
    "required": [
        "script"
    ],
    "type": "object"
}
```

## agent.searchCommands

Local registry helper: searchCommands. MCP clients use tools/list for discovery.

```json
{
    "additionalProperties": false,
    "properties": {
        "keyword": {
            "description": "The keyword argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "keyword"
    ],
    "type": "object"
}
```

## arrange.deleteBars

Arrange all Song tracks using a half-open bar interval; split crossing clips and preserve native offsets.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "maximum": 9999,
            "minimum": 1,
            "type": "integer"
        },
        "destinationBar": {
            "description": "The destinationBar argument for this command.",
            "maximum": 9999,
            "minimum": 0,
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "endBar": {
            "description": "The endBar argument for this command.",
            "maximum": 9999,
            "minimum": 1,
            "type": "integer"
        },
        "startBar": {
            "description": "The startBar argument for this command.",
            "maximum": 9999,
            "minimum": 0,
            "type": "integer"
        }
    },
    "required": [
        "startBar",
        "endBar"
    ],
    "type": "object"
}
```

## arrange.duplicateSection

Arrange all Song tracks using a half-open bar interval; split crossing clips and preserve native offsets.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "maximum": 9999,
            "minimum": 1,
            "type": "integer"
        },
        "destinationBar": {
            "description": "The destinationBar argument for this command.",
            "maximum": 9999,
            "minimum": 0,
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "endBar": {
            "description": "The endBar argument for this command.",
            "maximum": 9999,
            "minimum": 1,
            "type": "integer"
        },
        "startBar": {
            "description": "The startBar argument for this command.",
            "maximum": 9999,
            "minimum": 0,
            "type": "integer"
        }
    },
    "required": [
        "startBar",
        "endBar",
        "destinationBar"
    ],
    "type": "object"
}
```

## arrange.insertBars

Arrange all Song tracks using a half-open bar interval; split crossing clips and preserve native offsets.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "maximum": 9999,
            "minimum": 1,
            "type": "integer"
        },
        "destinationBar": {
            "description": "The destinationBar argument for this command.",
            "maximum": 9999,
            "minimum": 0,
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "endBar": {
            "description": "The endBar argument for this command.",
            "maximum": 9999,
            "minimum": 1,
            "type": "integer"
        },
        "startBar": {
            "description": "The startBar argument for this command.",
            "maximum": 9999,
            "minimum": 0,
            "type": "integer"
        }
    },
    "required": [
        "startBar",
        "bars"
    ],
    "type": "object"
}
```

## automation.addClip

Create an automation clip on an automation track.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "length": {
            "description": "The length argument for this command.",
            "type": "integer"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "position",
        "length"
    ],
    "type": "object"
}
```

## automation.addTarget

Add an addressable model as an automation target.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "nodes": {
            "description": "The nodes argument for this command.",
            "type": "array"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "target": {
            "description": "The target argument for this command.",
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "target"
    ],
    "type": "object"
}
```

## automation.createTrack

Create an automation track.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "parent": {
            "description": "The parent argument for this command.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        }
    },
    "type": "object"
}
```

## automation.listTargets

List addressable automation targets under an optional scope.

```json
{
    "additionalProperties": false,
    "properties": {
        "scope": {
            "description": "Model domain; effect includes slots on tracks and mixer channels. Omitted selects all domains.",
            "enum": [
                "track",
                "mixer",
                "effect",
                "song"
            ],
            "type": "string"
        }
    },
    "type": "object"
}
```

## automation.putValue

Write one model value at a tick position.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "pos": {
            "description": "The pos argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "number"
        }
    },
    "required": [
        "track",
        "clip",
        "pos",
        "value"
    ],
    "type": "object"
}
```

## automation.putValues

Write one or more model-value automation nodes.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "nodes": {
            "description": "The nodes argument for this command.",
            "type": "array"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "nodes"
    ],
    "type": "object"
}
```

## automation.removeNode

Remove the automation node at a tick position.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "pos": {
            "description": "The pos argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "pos"
    ],
    "type": "object"
}
```

## automation.removeNodes

Remove automation nodes between inclusive tick endpoints.

```json
{
    "additionalProperties": false,
    "oneOf": [
        {
            "properties": {
            },
            "required": [
                "range"
            ],
            "type": "object"
        },
        {
            "properties": {
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        }
    ],
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "end": {
            "description": "The end argument for this command.",
            "type": "integer"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "range": {
            "additionalProperties": false,
            "description": "Clip-local tick endpoints, both inclusive; reversed endpoints are normalized by LMMS.",
            "properties": {
                "end": {
                    "type": "integer"
                },
                "start": {
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        },
        "start": {
            "description": "The start argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## automation.setProgression

Set an automation curve progression type.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "type": {
            "description": "The type argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "track",
        "clip",
        "type"
    ],
    "type": "object"
}
```

## automation.setTension

Set a cubic automation curve tension.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "number"
        }
    },
    "required": [
        "track",
        "clip",
        "value"
    ],
    "type": "object"
}
```

## clip.create

Create a MIDI, sample or automation clip matching the track type.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "length": {
            "description": "The length argument for this command.",
            "type": "integer"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        },
        "start": {
            "description": "The start argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "type": {
            "description": "The type argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "track"
    ],
    "type": "object"
}
```

## clip.delete

Remove a clip.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## clip.duplicate

Duplicate a clip, by default immediately after the original.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## clip.get

Return a clip detail object.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## clip.list

List clips on a track.

```json
{
    "additionalProperties": false,
    "properties": {
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track"
    ],
    "type": "object"
}
```

## clip.move

Move a clip in ticks.

```json
{
    "additionalProperties": false,
    "properties": {
        "bar": {
            "description": "The bar argument for this command.",
            "type": "integer"
        },
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## clip.remove

Remove a clip.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## clip.resize

Set a clip length in ticks.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "length": {
            "description": "The length argument for this command.",
            "type": "integer"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "length"
    ],
    "type": "object"
}
```

## clip.setAutoResize

Set the clip autoresize.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "boolean"
        }
    },
    "required": [
        "track",
        "clip",
        "value"
    ],
    "type": "object"
}
```

## clip.setColor

Set the clip color.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "anyOf": [
                {
                    "type": "string"
                },
                {
                    "type": "null"
                }
            ],
            "description": "The value argument for this command."
        }
    },
    "required": [
        "track",
        "clip",
        "value"
    ],
    "type": "object"
}
```

## clip.setLength

Set a clip length in ticks.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "length": {
            "description": "The length argument for this command.",
            "type": "integer"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "length"
    ],
    "type": "object"
}
```

## clip.setMute

Set a clip mute state.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "boolean"
        }
    },
    "required": [
        "track",
        "clip",
        "value"
    ],
    "type": "object"
}
```

## clip.setPosition

Move a clip in ticks.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "position"
    ],
    "type": "object"
}
```

## clip.setStartTimeOffset

Set the clip starttimeoffset.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "track",
        "clip",
        "value"
    ],
    "type": "object"
}
```

## compose.arpeggio

Compose arpeggio as native MIDI notes; root uses LMMS C0=0, positions use ticks.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "maximum": 512,
            "minimum": 1,
            "type": "integer"
        },
        "clip": {
            "description": "The clip argument for this command.",
            "maximum": 2147483647,
            "minimum": 0,
            "type": "integer"
        },
        "division": {
            "description": "The division argument for this command.",
            "maximum": 192,
            "minimum": 1,
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "instrument": {
            "description": "The instrument argument for this command.",
            "type": "string"
        },
        "inversion": {
            "description": "The inversion argument for this command.",
            "maximum": 3,
            "minimum": 0,
            "type": "integer"
        },
        "mode": {
            "description": "The mode argument for this command.",
            "enum": [
                "up",
                "down",
                "updown",
                "random"
            ],
            "type": "string"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "progression": {
            "anyOf": [
                {
                    "type": "string"
                },
                {
                    "items": {
                        "type": "string"
                    },
                    "maxItems": 512,
                    "type": "array"
                }
            ],
            "description": "The progression argument for this command."
        },
        "rate": {
            "description": "The rate argument for this command.",
            "maximum": 192,
            "minimum": 1,
            "type": "integer"
        },
        "root": {
            "description": "The root argument for this command.",
            "maximum": 96,
            "minimum": 24,
            "type": "integer"
        },
        "seed": {
            "description": "The seed argument for this command.",
            "maximum": 2147483647,
            "minimum": -2147483648,
            "type": "integer"
        },
        "start": {
            "description": "The start argument for this command.",
            "maximum": 1919808,
            "minimum": 0,
            "type": "integer"
        },
        "style": {
            "description": "The style argument for this command.",
            "enum": [
                "four_on_floor",
                "rock",
                "trap"
            ],
            "type": "string"
        },
        "track": {
            "description": "The track argument for this command.",
            "maximum": 2147483647,
            "minimum": 0,
            "type": "integer"
        },
        "velocity": {
            "description": "The velocity argument for this command.",
            "maximum": 200,
            "minimum": 1,
            "type": "integer"
        }
    },
    "required": [
    ],
    "type": "object"
}
```

## compose.bassline

Compose bassline as native MIDI notes; root uses LMMS C0=0, positions use ticks.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "maximum": 512,
            "minimum": 1,
            "type": "integer"
        },
        "clip": {
            "description": "The clip argument for this command.",
            "maximum": 2147483647,
            "minimum": 0,
            "type": "integer"
        },
        "division": {
            "description": "The division argument for this command.",
            "maximum": 192,
            "minimum": 1,
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "instrument": {
            "description": "The instrument argument for this command.",
            "type": "string"
        },
        "inversion": {
            "description": "The inversion argument for this command.",
            "maximum": 3,
            "minimum": 0,
            "type": "integer"
        },
        "mode": {
            "description": "The mode argument for this command.",
            "enum": [
                "up",
                "down",
                "updown",
                "random"
            ],
            "type": "string"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "progression": {
            "anyOf": [
                {
                    "type": "string"
                },
                {
                    "items": {
                        "type": "string"
                    },
                    "maxItems": 512,
                    "type": "array"
                }
            ],
            "description": "The progression argument for this command."
        },
        "rate": {
            "description": "The rate argument for this command.",
            "maximum": 192,
            "minimum": 1,
            "type": "integer"
        },
        "root": {
            "description": "The root argument for this command.",
            "maximum": 96,
            "minimum": 24,
            "type": "integer"
        },
        "seed": {
            "description": "The seed argument for this command.",
            "maximum": 2147483647,
            "minimum": -2147483648,
            "type": "integer"
        },
        "start": {
            "description": "The start argument for this command.",
            "maximum": 1919808,
            "minimum": 0,
            "type": "integer"
        },
        "style": {
            "description": "The style argument for this command.",
            "enum": [
                "four_on_floor",
                "rock",
                "trap"
            ],
            "type": "string"
        },
        "track": {
            "description": "The track argument for this command.",
            "maximum": 2147483647,
            "minimum": 0,
            "type": "integer"
        },
        "velocity": {
            "description": "The velocity argument for this command.",
            "maximum": 200,
            "minimum": 1,
            "type": "integer"
        }
    },
    "required": [
    ],
    "type": "object"
}
```

## compose.chordProgression

Compose chordProgression as native MIDI notes; root uses LMMS C0=0, positions use ticks.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "maximum": 512,
            "minimum": 1,
            "type": "integer"
        },
        "clip": {
            "description": "The clip argument for this command.",
            "maximum": 2147483647,
            "minimum": 0,
            "type": "integer"
        },
        "division": {
            "description": "The division argument for this command.",
            "maximum": 192,
            "minimum": 1,
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "instrument": {
            "description": "The instrument argument for this command.",
            "type": "string"
        },
        "inversion": {
            "description": "The inversion argument for this command.",
            "maximum": 3,
            "minimum": 0,
            "type": "integer"
        },
        "mode": {
            "description": "The mode argument for this command.",
            "enum": [
                "up",
                "down",
                "updown",
                "random"
            ],
            "type": "string"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "progression": {
            "anyOf": [
                {
                    "type": "string"
                },
                {
                    "items": {
                        "type": "string"
                    },
                    "maxItems": 512,
                    "type": "array"
                }
            ],
            "description": "The progression argument for this command."
        },
        "rate": {
            "description": "The rate argument for this command.",
            "maximum": 192,
            "minimum": 1,
            "type": "integer"
        },
        "root": {
            "description": "The root argument for this command.",
            "maximum": 96,
            "minimum": 24,
            "type": "integer"
        },
        "seed": {
            "description": "The seed argument for this command.",
            "maximum": 2147483647,
            "minimum": -2147483648,
            "type": "integer"
        },
        "start": {
            "description": "The start argument for this command.",
            "maximum": 1919808,
            "minimum": 0,
            "type": "integer"
        },
        "style": {
            "description": "The style argument for this command.",
            "enum": [
                "four_on_floor",
                "rock",
                "trap"
            ],
            "type": "string"
        },
        "track": {
            "description": "The track argument for this command.",
            "maximum": 2147483647,
            "minimum": 0,
            "type": "integer"
        },
        "velocity": {
            "description": "The velocity argument for this command.",
            "maximum": 200,
            "minimum": 1,
            "type": "integer"
        }
    },
    "required": [
    ],
    "type": "object"
}
```

## compose.drumPattern

Compose drumPattern as native MIDI notes; root uses LMMS C0=0, positions use ticks.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "maximum": 512,
            "minimum": 1,
            "type": "integer"
        },
        "clip": {
            "description": "The clip argument for this command.",
            "maximum": 2147483647,
            "minimum": 0,
            "type": "integer"
        },
        "division": {
            "description": "The division argument for this command.",
            "maximum": 192,
            "minimum": 1,
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "instrument": {
            "description": "The instrument argument for this command.",
            "type": "string"
        },
        "inversion": {
            "description": "The inversion argument for this command.",
            "maximum": 3,
            "minimum": 0,
            "type": "integer"
        },
        "mode": {
            "description": "The mode argument for this command.",
            "enum": [
                "up",
                "down",
                "updown",
                "random"
            ],
            "type": "string"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "progression": {
            "anyOf": [
                {
                    "type": "string"
                },
                {
                    "items": {
                        "type": "string"
                    },
                    "maxItems": 512,
                    "type": "array"
                }
            ],
            "description": "The progression argument for this command."
        },
        "rate": {
            "description": "The rate argument for this command.",
            "maximum": 192,
            "minimum": 1,
            "type": "integer"
        },
        "root": {
            "description": "The root argument for this command.",
            "maximum": 96,
            "minimum": 24,
            "type": "integer"
        },
        "seed": {
            "description": "The seed argument for this command.",
            "maximum": 2147483647,
            "minimum": -2147483648,
            "type": "integer"
        },
        "start": {
            "description": "The start argument for this command.",
            "maximum": 1919808,
            "minimum": 0,
            "type": "integer"
        },
        "style": {
            "description": "The style argument for this command.",
            "enum": [
                "four_on_floor",
                "rock",
                "trap"
            ],
            "type": "string"
        },
        "track": {
            "description": "The track argument for this command.",
            "maximum": 2147483647,
            "minimum": 0,
            "type": "integer"
        },
        "velocity": {
            "description": "The velocity argument for this command.",
            "maximum": 200,
            "minimum": 1,
            "type": "integer"
        }
    },
    "required": [
    ],
    "type": "object"
}
```

## compose.melodyVariation

Duplicate a MIDI clip, optionally transpose, then apply seeded timing and velocity variation.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "maximum": 2147483647,
            "minimum": 0,
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "position": {
            "description": "The position argument for this command.",
            "maximum": 2147483647,
            "minimum": 0,
            "type": "integer"
        },
        "seed": {
            "description": "The seed argument for this command.",
            "maximum": 2147483647,
            "minimum": -2147483648,
            "type": "integer"
        },
        "semitones": {
            "description": "The semitones argument for this command.",
            "maximum": 127,
            "minimum": -127,
            "type": "integer"
        },
        "timing": {
            "description": "The timing argument for this command.",
            "maximum": 48,
            "minimum": 0,
            "type": "integer"
        },
        "track": {
            "description": "The track argument for this command.",
            "maximum": 2147483647,
            "minimum": 0,
            "type": "integer"
        },
        "velocity": {
            "description": "The velocity argument for this command.",
            "maximum": 200,
            "minimum": 0,
            "type": "integer"
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## config.get

Read a local configuration string.

```json
{
    "additionalProperties": false,
    "properties": {
        "group": {
            "description": "The group argument for this command.",
            "type": "string"
        },
        "key": {
            "description": "The key argument for this command.",
            "type": "string"
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "group",
        "key"
    ],
    "type": "object"
}
```

## config.set

Set a local configuration string; settings may require restart. Not part of project undo.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "group": {
            "description": "The group argument for this command.",
            "type": "string"
        },
        "key": {
            "description": "The key argument for this command.",
            "type": "string"
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "group",
        "key",
        "value"
    ],
    "type": "object"
}
```

## controller.add

Controller operation: add.

```json
{
    "additionalProperties": false,
    "properties": {
        "controller": {
            "description": "The controller argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "owner": {
            "description": "The owner argument for this command.",
            "type": "string"
        },
        "target": {
            "description": "The target argument for this command.",
            "type": "string"
        },
        "type": {
            "description": "The type argument for this command.",
            "type": "string"
        },
        "value": {
            "description": "The value argument for this command."
        }
    },
    "required": [
        "type"
    ],
    "type": "object"
}
```

## controller.connect

Controller operation: connect.

```json
{
    "additionalProperties": false,
    "properties": {
        "controller": {
            "description": "The controller argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "owner": {
            "description": "The owner argument for this command.",
            "type": "string"
        },
        "target": {
            "description": "The target argument for this command.",
            "type": "string"
        },
        "type": {
            "description": "The type argument for this command.",
            "type": "string"
        },
        "value": {
            "description": "The value argument for this command."
        }
    },
    "required": [
        "controller",
        "target"
    ],
    "type": "object"
}
```

## controller.list

Controller operation: list.

```json
{
    "additionalProperties": false,
    "properties": {
        "controller": {
            "description": "The controller argument for this command.",
            "type": "integer"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "owner": {
            "description": "The owner argument for this command.",
            "type": "string"
        },
        "target": {
            "description": "The target argument for this command.",
            "type": "string"
        },
        "type": {
            "description": "The type argument for this command.",
            "type": "string"
        },
        "value": {
            "description": "The value argument for this command."
        }
    },
    "type": "object"
}
```

## controller.remove

Controller operation: remove.

```json
{
    "additionalProperties": false,
    "properties": {
        "controller": {
            "description": "The controller argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "owner": {
            "description": "The owner argument for this command.",
            "type": "string"
        },
        "target": {
            "description": "The target argument for this command.",
            "type": "string"
        },
        "type": {
            "description": "The type argument for this command.",
            "type": "string"
        },
        "value": {
            "description": "The value argument for this command."
        }
    },
    "required": [
        "controller"
    ],
    "type": "object"
}
```

## controller.setParam

Controller operation: setParam.

```json
{
    "additionalProperties": false,
    "properties": {
        "controller": {
            "description": "The controller argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "owner": {
            "description": "The owner argument for this command.",
            "type": "string"
        },
        "target": {
            "description": "The target argument for this command.",
            "type": "string"
        },
        "type": {
            "description": "The type argument for this command.",
            "type": "string"
        },
        "value": {
            "description": "The value argument for this command."
        }
    },
    "required": [
        "controller",
        "name",
        "value"
    ],
    "type": "object"
}
```

## edit.humanize

Edit selected MIDI notes through midi.humanize.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "detune": {
            "description": "The detune argument for this command.",
            "type": "number"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "keys": {
            "description": "Optional key filter; omitted or empty selects all keys.",
            "items": {
                "description": "LMMS key number.",
                "maximum": 127,
                "minimum": 0,
                "type": "integer"
            },
            "type": "array"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "range": {
            "additionalProperties": false,
            "description": "Clip-local tick range: start inclusive, end exclusive; selects note starts.",
            "properties": {
                "end": {
                    "type": "integer"
                },
                "start": {
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        },
        "seed": {
            "description": "The seed argument for this command.",
            "type": "integer"
        },
        "timing": {
            "description": "The timing argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "velocity": {
            "description": "The velocity argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## edit.quantize

Edit selected MIDI notes through midi.quantize.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "grid": {
            "description": "Piano-roll note division, e.g. 16 for sixteenth notes, 24 for sixteenth-note triplets.",
            "enum": [
                1,
                2,
                4,
                8,
                16,
                32,
                64,
                3,
                6,
                12,
                24,
                48,
                96,
                192
            ],
            "type": "integer"
        },
        "keys": {
            "description": "Optional key filter; omitted or empty selects all keys.",
            "items": {
                "description": "LMMS key number.",
                "maximum": 127,
                "minimum": 0,
                "type": "integer"
            },
            "type": "array"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "range": {
            "additionalProperties": false,
            "description": "Clip-local tick range: start inclusive, end exclusive; selects note starts.",
            "properties": {
                "end": {
                    "type": "integer"
                },
                "start": {
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        },
        "strength": {
            "description": "Fraction of the distance to the grid; defaults to 1.",
            "maximum": 1,
            "minimum": 0,
            "type": "number"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "grid"
    ],
    "type": "object"
}
```

## edit.transpose

Edit selected MIDI notes through midi.transpose.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "keys": {
            "description": "Optional key filter; omitted or empty selects all keys.",
            "items": {
                "description": "LMMS key number.",
                "maximum": 127,
                "minimum": 0,
                "type": "integer"
            },
            "type": "array"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "range": {
            "additionalProperties": false,
            "description": "Clip-local tick range: start inclusive, end exclusive; selects note starts.",
            "properties": {
                "end": {
                    "type": "integer"
                },
                "start": {
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        },
        "semitones": {
            "description": "The semitones argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "semitones"
    ],
    "type": "object"
}
```

## effect.add

Add an effect plugin to an effect chain.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "owner": {
            "description": "The owner argument for this command.",
            "type": "string"
        },
        "plugin": {
            "description": "The plugin argument for this command.",
            "type": "string"
        },
        "subKey": {
            "description": "The subKey argument for this command.",
            "properties": {
                "attributes": {
                    "additionalProperties": {
                        "type": "string"
                    },
                    "type": "object"
                },
                "name": {
                    "type": "string"
                }
            },
            "required": [
                "attributes"
            ],
            "type": "object"
        }
    },
    "required": [
        "owner",
        "plugin"
    ],
    "type": "object"
}
```

## effect.getParams

List L1 serialized effect parameters.

```json
{
    "additionalProperties": false,
    "properties": {
        "owner": {
            "description": "The owner argument for this command.",
            "type": "string"
        },
        "slot": {
            "description": "The slot argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "owner",
        "slot"
    ],
    "type": "object"
}
```

## effect.listAvailable

List available effect plugins.

```json
{
    "additionalProperties": false,
    "properties": {
        "kind": {
            "description": "The kind argument for this command.",
            "type": "string"
        }
    },
    "type": "object"
}
```

## effect.move

Move an effect to another slot.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "owner": {
            "description": "The owner argument for this command.",
            "type": "string"
        },
        "slot": {
            "description": "The slot argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "owner",
        "slot",
        "index"
    ],
    "type": "object"
}
```

## effect.remove

Remove an effect from an effect chain.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "owner": {
            "description": "The owner argument for this command.",
            "type": "string"
        },
        "slot": {
            "description": "The slot argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "owner",
        "slot"
    ],
    "type": "object"
}
```

## effect.setEnabled

Set an effect enabled state.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "owner": {
            "description": "The owner argument for this command.",
            "type": "string"
        },
        "slot": {
            "description": "The slot argument for this command.",
            "type": "integer"
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "boolean"
        }
    },
    "required": [
        "owner",
        "slot",
        "value"
    ],
    "type": "object"
}
```

## effect.setParam

Set one L1 serialized effect parameter.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "owner": {
            "description": "The owner argument for this command.",
            "type": "string"
        },
        "slot": {
            "description": "The slot argument for this command.",
            "type": "integer"
        },
        "value": {
            "description": "The value argument for this command."
        }
    },
    "required": [
        "owner",
        "slot",
        "name",
        "value"
    ],
    "type": "object"
}
```

## effect.setWetDry

Set an effect wet/dry value.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "owner": {
            "description": "The owner argument for this command.",
            "type": "string"
        },
        "slot": {
            "description": "The slot argument for this command.",
            "type": "integer"
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "number"
        }
    },
    "required": [
        "owner",
        "slot",
        "value"
    ],
    "type": "object"
}
```

## export.audio

Start a local audio render; returns a task for export.status/cancel. Range uses song ticks. Output files are committed only on success.

```json
{
    "additionalProperties": false,
    "properties": {
        "asLoop": {
            "description": "The asLoop argument for this command.",
            "type": "boolean"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "format": {
            "description": "The format argument for this command.",
            "enum": [
                "wav",
                "wave",
                "flac",
                "ogg",
                "mp3"
            ],
            "type": "string"
        },
        "loopCount": {
            "description": "The loopCount argument for this command.",
            "maximum": 512,
            "minimum": 1,
            "type": "integer"
        },
        "overwrite": {
            "description": "The overwrite argument for this command.",
            "type": "boolean"
        },
        "path": {
            "description": "The path argument for this command.",
            "minLength": 1,
            "type": "string"
        },
        "quality": {
            "additionalProperties": false,
            "description": "The quality argument for this command.",
            "properties": {
                "bitDepth": {
                    "enum": [
                        16,
                        24,
                        32
                    ],
                    "type": "integer"
                },
                "bitrate": {
                    "maximum": 512,
                    "minimum": 8,
                    "type": "integer"
                },
                "compression": {
                    "maximum": 1,
                    "minimum": 0,
                    "type": "number"
                },
                "sampleRate": {
                    "maximum": 384000,
                    "minimum": 8000,
                    "type": "integer"
                },
                "stereoMode": {
                    "enum": [
                        "mono",
                        "stereo",
                        "joint"
                    ],
                    "type": "string"
                }
            },
            "required": [
            ],
            "type": "object"
        },
        "range": {
            "additionalProperties": false,
            "description": "The range argument for this command.",
            "properties": {
                "end": {
                    "maximum": 1919808,
                    "minimum": 1,
                    "type": "integer"
                },
                "start": {
                    "maximum": 1919808,
                    "minimum": 0,
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        }
    },
    "required": [
        "path"
    ],
    "type": "object"
}
```

## export.cancel

Cancel an audio export and restore the device and transport. Completed/cancelled tasks remain queryable.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "task": {
            "description": "The task argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "task"
    ],
    "type": "object"
}
```

## export.midi

Export the project through the native MIDI filter; overwrite requires explicit permission.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "overwrite": {
            "description": "The overwrite argument for this command.",
            "type": "boolean"
        },
        "path": {
            "description": "The path argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "path"
    ],
    "type": "object"
}
```

## export.status

Query an audio export task, or list up to 64 recent tasks when task is omitted.

```json
{
    "additionalProperties": false,
    "properties": {
        "task": {
            "description": "The task argument for this command.",
            "type": "string"
        }
    },
    "required": [
    ],
    "type": "object"
}
```

## history.redo

Redo the most recently undone project change.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        }
    },
    "type": "object"
}
```

## history.rollbackBatch

Roll back the entire active batch, including nested batches.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        }
    },
    "type": "object"
}
```

## history.status

Return undo, redo and active batch depths.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Preview the operation without changing project history.",
            "type": "boolean"
        }
    },
    "type": "object"
}
```

## history.undo

Undo the most recent project change.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        }
    },
    "type": "object"
}
```

## import.hydrogen

Import a local hydrogen file without dialogs; changes form one undo step.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "path": {
            "description": "The path argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "path"
    ],
    "type": "object"
}
```

## import.midi

Import a local midi file without dialogs; changes form one undo step.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "path": {
            "description": "The path argument for this command.",
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "path"
    ],
    "type": "object"
}
```

## import.sampleToTrack

Create a sample clip from a local audio file, optionally on an existing sample track.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "path": {
            "description": "The path argument for this command.",
            "type": "string"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "path"
    ],
    "type": "object"
}
```

## instrument.getParams

List native parameter metadata with serialized-state fallback.

```json
{
    "additionalProperties": false,
    "properties": {
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track"
    ],
    "type": "object"
}
```

## instrument.load

Load an instrument plugin onto an instrument track.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "path": {
            "description": "The path argument for this command.",
            "type": "string"
        },
        "plugin": {
            "description": "The plugin argument for this command.",
            "type": "string"
        },
        "subKey": {
            "description": "The subKey argument for this command.",
            "properties": {
                "attributes": {
                    "additionalProperties": {
                        "type": "string"
                    },
                    "type": "object"
                },
                "name": {
                    "type": "string"
                }
            },
            "required": [
                "attributes"
            ],
            "type": "object"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "plugin"
    ],
    "type": "object"
}
```

## instrument.loadPreset

Instrument operation: loadPreset.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "path": {
            "description": "The path argument for this command.",
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "path"
    ],
    "type": "object"
}
```

## instrument.savePreset

Instrument operation: savePreset.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "overwrite": {
            "description": "The overwrite argument for this command.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "path": {
            "description": "The path argument for this command.",
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "path"
    ],
    "type": "object"
}
```

## instrument.setArpeggio

Instrument operation: setArpeggio.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "enabled": {
            "description": "The enabled argument for this command.",
            "type": "boolean"
        },
        "params": {
            "description": "The params argument for this command.",
            "type": "object"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "enabled"
    ],
    "type": "object"
}
```

## instrument.setBaseNote

Set an instrument-track base note.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "track",
        "value"
    ],
    "type": "object"
}
```

## instrument.setMidiIn

Instrument operation: setMidiIn.

```json
{
    "additionalProperties": false,
    "properties": {
        "channel": {
            "description": "The channel argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "enabled": {
            "description": "The enabled argument for this command.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "port": {
            "description": "The port argument for this command.",
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track"
    ],
    "type": "object"
}
```

## instrument.setMidiOut

Instrument operation: setMidiOut.

```json
{
    "additionalProperties": false,
    "properties": {
        "channel": {
            "description": "The channel argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "enabled": {
            "description": "The enabled argument for this command.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "port": {
            "description": "The port argument for this command.",
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track"
    ],
    "type": "object"
}
```

## instrument.setNoteStacking

Instrument operation: setNoteStacking.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "enabled": {
            "description": "The enabled argument for this command.",
            "type": "boolean"
        },
        "params": {
            "description": "The params argument for this command.",
            "type": "object"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "enabled"
    ],
    "type": "object"
}
```

## instrument.setPanning

Set an instrument-track panning value.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "number"
        }
    },
    "required": [
        "track",
        "value"
    ],
    "type": "object"
}
```

## instrument.setParam

Set a native parameter with type/range validation, or a serialized parameter.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command."
        }
    },
    "required": [
        "track",
        "name",
        "value"
    ],
    "type": "object"
}
```

## instrument.setParameters

Set one or more instrument-track parameters.

```json
{
    "additionalProperties": false,
    "properties": {
        "baseNote": {
            "description": "The baseNote argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "panning": {
            "description": "The panning argument for this command.",
            "type": "number"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "pitch": {
            "description": "The pitch argument for this command.",
            "type": "number"
        },
        "pitchRange": {
            "description": "The pitchRange argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "volume": {
            "description": "The volume argument for this command.",
            "type": "number"
        }
    },
    "required": [
        "track"
    ],
    "type": "object"
}
```

## instrument.setPiano

Instrument operation: setPiano.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "enabled": {
            "description": "The enabled argument for this command.",
            "type": "boolean"
        },
        "params": {
            "additionalProperties": false,
            "description": "The params argument for this command.",
            "properties": {
                "key": {
                    "type": "integer"
                },
                "velocity": {
                    "type": "integer"
                }
            },
            "type": "object"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "enabled"
    ],
    "type": "object"
}
```

## instrument.setPitch

Set instrument-track pitch in cents within its current pitch range.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "number"
        }
    },
    "required": [
        "track",
        "value"
    ],
    "type": "object"
}
```

## instrument.setPitchRange

Set an instrument-track pitch range.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "track",
        "value"
    ],
    "type": "object"
}
```

## instrument.setVolume

Set an instrument-track volume.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "number"
        }
    },
    "required": [
        "track",
        "value"
    ],
    "type": "object"
}
```

## midi.addNotes

Add MIDI notes to a MIDI clip.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "notes": {
            "description": "The notes argument for this command.",
            "items": {
                "additionalProperties": false,
                "properties": {
                    "key": {
                        "description": "LMMS key number.",
                        "maximum": 127,
                        "minimum": 0,
                        "type": "integer"
                    },
                    "length": {
                        "description": "Note duration in ticks.",
                        "maximum": 1919808,
                        "minimum": 1,
                        "type": "integer"
                    },
                    "panning": {
                        "maximum": 100,
                        "minimum": -100,
                        "type": "integer"
                    },
                    "position": {
                        "description": "Clip-local start in ticks.",
                        "maximum": 1919808,
                        "minimum": 0,
                        "type": "integer"
                    },
                    "volume": {
                        "description": "Note volume in LMMS percent units.",
                        "maximum": 200,
                        "minimum": 0,
                        "type": "integer"
                    }
                },
                "required": [
                    "position",
                    "length",
                    "key"
                ],
                "type": "object"
            },
            "maxItems": 4096,
            "type": "array"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "notes"
    ],
    "type": "object"
}
```

## midi.clearNotes

Remove all MIDI notes from a clip.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## midi.getNotes

Return a filtered page of MIDI notes with their current indices.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "keys": {
            "description": "Optional key filter; omitted or empty selects all keys.",
            "items": {
                "description": "LMMS key number.",
                "maximum": 127,
                "minimum": 0,
                "type": "integer"
            },
            "type": "array"
        },
        "page": {
            "description": "Zero-based page; defaults to zero.",
            "minimum": 0,
            "type": "integer"
        },
        "pageSize": {
            "description": "Notes per page; defaults to 256.",
            "maximum": 4096,
            "minimum": 1,
            "type": "integer"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "range": {
            "additionalProperties": false,
            "description": "Clip-local tick range: start inclusive, end exclusive; selects note starts.",
            "properties": {
                "end": {
                    "type": "integer"
                },
                "start": {
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## midi.humanize

Apply deterministic timing, velocity, and detune variation to MIDI notes.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "detune": {
            "description": "The detune argument for this command.",
            "type": "number"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "keys": {
            "description": "Optional key filter; omitted or empty selects all keys.",
            "items": {
                "description": "LMMS key number.",
                "maximum": 127,
                "minimum": 0,
                "type": "integer"
            },
            "type": "array"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "range": {
            "additionalProperties": false,
            "description": "Clip-local tick range: start inclusive, end exclusive; selects note starts.",
            "properties": {
                "end": {
                    "type": "integer"
                },
                "start": {
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        },
        "seed": {
            "description": "The seed argument for this command.",
            "type": "integer"
        },
        "timing": {
            "description": "The timing argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "velocity": {
            "description": "The velocity argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## midi.quantize

Quantize matching note starts using the piano-roll grid.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "grid": {
            "description": "Piano-roll note division, e.g. 16 for sixteenth notes, 24 for sixteenth-note triplets.",
            "enum": [
                1,
                2,
                4,
                8,
                16,
                32,
                64,
                3,
                6,
                12,
                24,
                48,
                96,
                192
            ],
            "type": "integer"
        },
        "keys": {
            "description": "Optional key filter; omitted or empty selects all keys.",
            "items": {
                "description": "LMMS key number.",
                "maximum": 127,
                "minimum": 0,
                "type": "integer"
            },
            "type": "array"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "range": {
            "additionalProperties": false,
            "description": "Clip-local tick range: start inclusive, end exclusive; selects note starts.",
            "properties": {
                "end": {
                    "type": "integer"
                },
                "start": {
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        },
        "strength": {
            "description": "Fraction of the distance to the grid; defaults to 1.",
            "maximum": 1,
            "minimum": 0,
            "type": "number"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "grid"
    ],
    "type": "object"
}
```

## midi.removeNotes

Remove notes matching optional start range and key filters; no filters removes all notes.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "keys": {
            "description": "Optional key filter; omitted or empty selects all keys.",
            "items": {
                "description": "LMMS key number.",
                "maximum": 127,
                "minimum": 0,
                "type": "integer"
            },
            "type": "array"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "range": {
            "additionalProperties": false,
            "description": "Clip-local tick range: start inclusive, end exclusive; selects note starts.",
            "properties": {
                "end": {
                    "type": "integer"
                },
                "start": {
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## midi.setClipType

Set a MIDI clip to beat or melody mode.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "type": {
            "description": "The type argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "track",
        "clip",
        "type"
    ],
    "type": "object"
}
```

## midi.setSteps

Set a MIDI clip's number of step positions.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "steps": {
            "description": "The steps argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "steps"
    ],
    "type": "object"
}
```

## midi.transpose

Transpose matching notes; fail atomically if any result is outside the LMMS key range.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "keys": {
            "description": "Optional key filter; omitted or empty selects all keys.",
            "items": {
                "description": "LMMS key number.",
                "maximum": 127,
                "minimum": 0,
                "type": "integer"
            },
            "type": "array"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "range": {
            "additionalProperties": false,
            "description": "Clip-local tick range: start inclusive, end exclusive; selects note starts.",
            "properties": {
                "end": {
                    "type": "integer"
                },
                "start": {
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        },
        "semitones": {
            "description": "The semitones argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "semitones"
    ],
    "type": "object"
}
```

## midi.updateNote

Update a note by its current index and return its index after sorting.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "fields": {
            "additionalProperties": false,
            "description": "The fields argument for this command.",
            "properties": {
                "key": {
                    "description": "LMMS key number.",
                    "maximum": 127,
                    "minimum": 0,
                    "type": "integer"
                },
                "length": {
                    "description": "Note duration in ticks.",
                    "maximum": 1919808,
                    "minimum": 1,
                    "type": "integer"
                },
                "panning": {
                    "maximum": 100,
                    "minimum": -100,
                    "type": "integer"
                },
                "position": {
                    "description": "Clip-local start in ticks.",
                    "maximum": 1919808,
                    "minimum": 0,
                    "type": "integer"
                },
                "volume": {
                    "description": "Note volume in LMMS percent units.",
                    "maximum": 200,
                    "minimum": 0,
                    "type": "integer"
                }
            },
            "type": "object"
        },
        "note": {
            "description": "The note argument for this command.",
            "type": "integer"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "note",
        "fields"
    ],
    "type": "object"
}
```

## mix.gainStaging

Adjust selected mixer gains from caller-provided measured peakDb to targetDb minus headroomDb; no automatic audio analysis.

```json
{
    "additionalProperties": false,
    "properties": {
        "channels": {
            "description": "The channels argument for this command.",
            "items": {
                "maximum": 2147483647,
                "minimum": 0,
                "type": "integer"
            },
            "maxItems": 512,
            "type": "array"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "headroomDb": {
            "description": "The headroomDb argument for this command.",
            "maximum": 60,
            "minimum": 0,
            "type": "number"
        },
        "peakDb": {
            "description": "The peakDb argument for this command.",
            "maximum": 60,
            "minimum": -120,
            "type": "number"
        },
        "targetDb": {
            "description": "The targetDb argument for this command.",
            "maximum": 0,
            "minimum": -120,
            "type": "number"
        }
    },
    "required": [
        "channels",
        "peakDb"
    ],
    "type": "object"
}
```

## mixer.addSend

Create a mixer send.

```json
{
    "additionalProperties": false,
    "properties": {
        "amount": {
            "description": "The amount argument for this command.",
            "type": "number"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "from": {
            "description": "The from argument for this command.",
            "type": "integer"
        },
        "to": {
            "description": "The to argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "from",
        "to"
    ],
    "type": "object"
}
```

## mixer.clearChannel

Clear all routes and effects from a mixer channel.

```json
{
    "additionalProperties": false,
    "properties": {
        "channel": {
            "description": "The channel argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        }
    },
    "required": [
        "channel"
    ],
    "type": "object"
}
```

## mixer.getChannel

Return a mixer channel detail object.

```json
{
    "additionalProperties": false,
    "properties": {
        "channel": {
            "description": "The channel argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "channel"
    ],
    "type": "object"
}
```

## mixer.getMaster

Return the master mixer channel detail object.

```json
{
    "additionalProperties": false,
    "properties": {
    },
    "type": "object"
}
```

## mixer.listChannels

List all mixer channels.

```json
{
    "additionalProperties": false,
    "properties": {
    },
    "type": "object"
}
```

## mixer.removeSend

Remove a mixer send.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "from": {
            "description": "The from argument for this command.",
            "type": "integer"
        },
        "to": {
            "description": "The to argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "from",
        "to"
    ],
    "type": "object"
}
```

## mixer.setColor

Set or clear a mixer channel color.

```json
{
    "additionalProperties": false,
    "properties": {
        "channel": {
            "description": "The channel argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "channel",
        "value"
    ],
    "type": "object"
}
```

## mixer.setMute

Set a mixer channel mute state.

```json
{
    "additionalProperties": false,
    "properties": {
        "channel": {
            "description": "The channel argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "boolean"
        }
    },
    "required": [
        "channel",
        "value"
    ],
    "type": "object"
}
```

## mixer.setName

Set a mixer channel name.

```json
{
    "additionalProperties": false,
    "oneOf": [
        {
            "properties": {
            },
            "required": [
                "name"
            ],
            "type": "object"
        },
        {
            "properties": {
            },
            "required": [
                "value"
            ],
            "type": "object"
        }
    ],
    "properties": {
        "channel": {
            "description": "The channel argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "channel"
    ],
    "type": "object"
}
```

## mixer.setSendAmount

Set a mixer send amount.

```json
{
    "additionalProperties": false,
    "properties": {
        "amount": {
            "description": "The amount argument for this command.",
            "type": "number"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "from": {
            "description": "The from argument for this command.",
            "type": "integer"
        },
        "to": {
            "description": "The to argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "from",
        "to",
        "amount"
    ],
    "type": "object"
}
```

## mixer.setSolo

Set a mixer channel solo state.

```json
{
    "additionalProperties": false,
    "properties": {
        "channel": {
            "description": "The channel argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "boolean"
        }
    },
    "required": [
        "channel",
        "value"
    ],
    "type": "object"
}
```

## mixer.setVolume

Set a mixer channel volume.

```json
{
    "additionalProperties": false,
    "properties": {
        "channel": {
            "description": "The channel argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "number"
        }
    },
    "required": [
        "channel",
        "value"
    ],
    "type": "object"
}
```

## model.getValue

Return an addressable model value.

```json
{
    "additionalProperties": false,
    "properties": {
        "path": {
            "description": "The path argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "path"
    ],
    "type": "object"
}
```

## model.list

List addressable models, optionally under a prefix.

```json
{
    "additionalProperties": false,
    "properties": {
        "prefix": {
            "description": "The prefix argument for this command.",
            "type": "string"
        }
    },
    "type": "object"
}
```

## model.search

Search addressable models by path or name.

```json
{
    "additionalProperties": false,
    "properties": {
        "keyword": {
            "description": "The keyword argument for this command.",
            "type": "string"
        }
    },
    "type": "object"
}
```

## model.setValue

Set an addressable model value.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "path": {
            "description": "The path argument for this command.",
            "type": "string"
        },
        "value": {
            "anyOf": [
                {
                    "type": "number"
                },
                {
                    "type": "boolean"
                }
            ],
            "description": "The value argument for this command."
        }
    },
    "required": [
        "path",
        "value"
    ],
    "type": "object"
}
```

## pattern.create

Pattern operation: create.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "length": {
            "description": "The length argument for this command.",
            "type": "integer"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "pattern": {
            "description": "The pattern argument for this command.",
            "type": "integer"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        }
    },
    "type": "object"
}
```

## pattern.get

Pattern operation: get.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "type": "integer"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "length": {
            "description": "The length argument for this command.",
            "type": "integer"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "pattern": {
            "description": "The pattern argument for this command.",
            "type": "integer"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "pattern"
    ],
    "type": "object"
}
```

## pattern.list

Pattern operation: list.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "type": "integer"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "length": {
            "description": "The length argument for this command.",
            "type": "integer"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "pattern": {
            "description": "The pattern argument for this command.",
            "type": "integer"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        }
    },
    "type": "object"
}
```

## pattern.placeInSong

Pattern operation: placeInSong.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "length": {
            "description": "The length argument for this command.",
            "type": "integer"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "pattern": {
            "description": "The pattern argument for this command.",
            "type": "integer"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "pattern",
        "position"
    ],
    "type": "object"
}
```

## pattern.remove

Pattern operation: remove.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "length": {
            "description": "The length argument for this command.",
            "type": "integer"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "pattern": {
            "description": "The pattern argument for this command.",
            "type": "integer"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "pattern"
    ],
    "type": "object"
}
```

## pattern.removeFromSong

Remove a song reference to a pattern.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## pattern.rename

Pattern operation: rename.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "length": {
            "description": "The length argument for this command.",
            "type": "integer"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "pattern": {
            "description": "The pattern argument for this command.",
            "type": "integer"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "pattern",
        "name"
    ],
    "type": "object"
}
```

## pattern.setLength

Pattern operation: setLength.

```json
{
    "additionalProperties": false,
    "properties": {
        "bars": {
            "description": "The bars argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "length": {
            "description": "The length argument for this command.",
            "type": "integer"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "pattern": {
            "description": "The pattern argument for this command.",
            "type": "integer"
        },
        "position": {
            "description": "The position argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "pattern",
        "bars"
    ],
    "type": "object"
}
```

## query.clipDetail

Return a clip detail object.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## query.mixerState

Return the current mixer channel state.

```json
{
    "additionalProperties": false,
    "properties": {
    },
    "type": "object"
}
```

## query.modelSearch

Search addressable models by path or name.

```json
{
    "additionalProperties": false,
    "properties": {
        "keyword": {
            "description": "The keyword argument for this command.",
            "type": "string"
        }
    },
    "type": "object"
}
```

## query.notes

Return a filtered page of MIDI notes with their current indices.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "keys": {
            "description": "Optional key filter; omitted or empty selects all keys.",
            "items": {
                "description": "LMMS key number.",
                "maximum": 127,
                "minimum": 0,
                "type": "integer"
            },
            "type": "array"
        },
        "page": {
            "description": "Zero-based page; defaults to zero.",
            "minimum": 0,
            "type": "integer"
        },
        "pageSize": {
            "description": "Notes per page; defaults to 256.",
            "maximum": 4096,
            "minimum": 1,
            "type": "integer"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "range": {
            "additionalProperties": false,
            "description": "Clip-local tick range: start inclusive, end exclusive; selects note starts.",
            "properties": {
                "end": {
                    "type": "integer"
                },
                "start": {
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## query.songSummary

Return a compact summary or full track details.

```json
{
    "additionalProperties": false,
    "properties": {
        "detail": {
            "description": "The detail argument for this command.",
            "enum": [
                "compact",
                "full"
            ],
            "type": "string"
        }
    },
    "type": "object"
}
```

## query.trackDetail

Return a track detail object.

```json
{
    "additionalProperties": false,
    "properties": {
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track"
    ],
    "type": "object"
}
```

## render.preview

Render a local temporary WAV and return the export task; the caller plays the file and removes it when finished.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "range": {
            "additionalProperties": false,
            "description": "The range argument for this command.",
            "properties": {
                "end": {
                    "maximum": 1919808,
                    "minimum": 1,
                    "type": "integer"
                },
                "start": {
                    "maximum": 1919808,
                    "minimum": 0,
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        }
    },
    "required": [
    ],
    "type": "object"
}
```

## sample.getInfo

Return metadata and peak information for a sample clip.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## sample.setFile

Load a local audio file into a sample clip.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "path": {
            "description": "The path argument for this command.",
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip",
        "path"
    ],
    "type": "object"
}
```

## sample.setOffset

Set a sample clip's timeline offset in ticks.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "track",
        "clip",
        "value"
    ],
    "type": "object"
}
```

## sample.setReversed

Set whether a sample clip plays in reverse.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "boolean"
        }
    },
    "required": [
        "track",
        "clip",
        "value"
    ],
    "type": "object"
}
```

## scale.get

Project scale operation: get.

```json
{
    "additionalProperties": false,
    "properties": {
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        }
    },
    "type": "object"
}
```

## scale.set

Project scale operation: set.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "root": {
            "description": "The root argument for this command.",
            "type": "integer"
        },
        "semitones": {
            "description": "The semitones argument for this command.",
            "items": {
                "type": "integer"
            },
            "type": "array"
        },
        "type": {
            "description": "The type argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "root",
        "type"
    ],
    "type": "object"
}
```

## scale.snapNotes

Project scale operation: snapNotes.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "keys": {
            "description": "Optional key filter; omitted or empty selects all keys.",
            "items": {
                "description": "LMMS key number.",
                "maximum": 127,
                "minimum": 0,
                "type": "integer"
            },
            "type": "array"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "range": {
            "additionalProperties": false,
            "description": "Clip-local tick range: start inclusive, end exclusive; selects note starts.",
            "properties": {
                "end": {
                    "type": "integer"
                },
                "start": {
                    "type": "integer"
                }
            },
            "required": [
                "start",
                "end"
            ],
            "type": "object"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## song.clearProject

Clear every track and controller from the current project.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        }
    },
    "type": "object"
}
```

## song.getInfo

Return a compact summary of the current song.

```json
{
    "additionalProperties": false,
    "properties": {
    },
    "type": "object"
}
```

## song.load

Load a validated local LMMS project file.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "path": {
            "description": "The path argument for this command.",
            "type": "string"
        },
        "promptSave": {
            "description": "The promptSave argument for this command.",
            "type": "boolean"
        }
    },
    "required": [
        "path"
    ],
    "type": "object"
}
```

## song.save

Save the current project, optionally as a resource bundle.

```json
{
    "additionalProperties": false,
    "properties": {
        "asBundle": {
            "description": "The asBundle argument for this command.",
            "type": "boolean"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "overwrite": {
            "description": "The overwrite argument for this command.",
            "type": "boolean"
        },
        "path": {
            "description": "The path argument for this command.",
            "type": "string"
        }
    },
    "type": "object"
}
```

## song.setMasterPitch

Set the master pitch in semitones.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "semitones": {
            "description": "The semitones argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "semitones"
    ],
    "type": "object"
}
```

## song.setMasterVolume

Set the master volume.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "value"
    ],
    "type": "object"
}
```

## song.setPlayMode

Select Song, Pattern, MidiClip or AutomationClip playback.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "fromBar": {
            "description": "The fromBar argument for this command.",
            "type": "integer"
        },
        "loop": {
            "description": "The loop argument for this command.",
            "type": "boolean"
        },
        "mode": {
            "description": "The mode argument for this command.",
            "type": "string"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "pattern": {
            "description": "The pattern argument for this command.",
            "type": "integer"
        },
        "ticks": {
            "description": "The ticks argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "mode"
    ],
    "type": "object"
}
```

## song.setTempo

Set the song tempo in BPM.

```json
{
    "additionalProperties": false,
    "properties": {
        "bpm": {
            "description": "The bpm argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        }
    },
    "required": [
        "bpm"
    ],
    "type": "object"
}
```

## song.setTimeSignature

Set the song time signature.

```json
{
    "additionalProperties": false,
    "properties": {
        "denominator": {
            "description": "The denominator argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "numerator": {
            "description": "The numerator argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "numerator",
        "denominator"
    ],
    "type": "object"
}
```

## track.clone

Clone a track with its clips and plugin settings.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track"
    ],
    "type": "object"
}
```

## track.create

Create an instrument, sample, or automation track.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "index": {
            "description": "The index argument for this command.",
            "type": "integer"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "parent": {
            "description": "The parent argument for this command.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "type": {
            "description": "The type argument for this command.",
            "type": "string"
        }
    },
    "type": "object"
}
```

## track.delete

Remove a track.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track"
    ],
    "type": "object"
}
```

## track.get

Return a track detail object.

```json
{
    "additionalProperties": false,
    "properties": {
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track"
    ],
    "type": "object"
}
```

## track.list

List tracks in the song or pattern container.

```json
{
    "additionalProperties": false,
    "properties": {
        "parent": {
            "description": "The parent argument for this command.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        }
    },
    "type": "object"
}
```

## track.move

Move a track to a zero-based position in its container.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "newIndex": {
            "description": "The newIndex argument for this command.",
            "type": "integer"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "newIndex"
    ],
    "type": "object"
}
```

## track.remove

Remove a track.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track"
    ],
    "type": "object"
}
```

## track.setColor

Set the track color.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "anyOf": [
                {
                    "type": "string"
                },
                {
                    "type": "null"
                }
            ],
            "description": "The value argument for this command."
        }
    },
    "required": [
        "track",
        "value"
    ],
    "type": "object"
}
```

## track.setHeight

Set the track height.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "track",
        "value"
    ],
    "type": "object"
}
```

## track.setMixerChannel

Set the track mixerchannel.

```json
{
    "additionalProperties": false,
    "properties": {
        "channel": {
            "description": "The channel argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "channel"
    ],
    "type": "object"
}
```

## track.setMute

Set a track mute state.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "boolean"
        }
    },
    "required": [
        "track",
        "value"
    ],
    "type": "object"
}
```

## track.setMuted

Set a track mute state.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "boolean"
        }
    },
    "required": [
        "track",
        "value"
    ],
    "type": "object"
}
```

## track.setName

Set a track name.

```json
{
    "additionalProperties": false,
    "oneOf": [
        {
            "properties": {
            },
            "required": [
                "name"
            ],
            "type": "object"
        },
        {
            "properties": {
            },
            "required": [
                "value"
            ],
            "type": "object"
        }
    ],
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "name": {
            "description": "The name argument for this command.",
            "type": "string"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "string"
        }
    },
    "required": [
        "track"
    ],
    "type": "object"
}
```

## track.setSolo

Set a track solo state.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        },
        "value": {
            "description": "The value argument for this command.",
            "type": "boolean"
        }
    },
    "required": [
        "track",
        "value"
    ],
    "type": "object"
}
```

## transport.getPosition

Return the current playback position.

```json
{
    "additionalProperties": false,
    "properties": {
    },
    "type": "object"
}
```

## transport.play

Start playback in the selected mode.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "fromBar": {
            "description": "The fromBar argument for this command.",
            "type": "integer"
        },
        "loop": {
            "description": "The loop argument for this command.",
            "type": "boolean"
        },
        "mode": {
            "description": "The mode argument for this command.",
            "type": "string"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "pattern": {
            "description": "The pattern argument for this command.",
            "type": "integer"
        },
        "ticks": {
            "description": "The ticks argument for this command.",
            "type": "integer"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "type": "object"
}
```

## transport.playSong

Start song playback.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        }
    },
    "type": "object"
}
```

## transport.previewClip

Preview one MIDI clip without changing the project.

```json
{
    "additionalProperties": false,
    "properties": {
        "clip": {
            "description": "The clip argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "loop": {
            "description": "The loop argument for this command.",
            "type": "boolean"
        },
        "parent": {
            "description": "Track container; defaults to song. A track path carries its own container.",
            "enum": [
                "song",
                "pattern"
            ],
            "type": "string"
        },
        "track": {
            "anyOf": [
                {
                    "type": "integer"
                },
                {
                    "type": "string"
                }
            ],
            "description": "Track index in parent, unique track name, or returned song/track:N or pattern/track:N path. Indices and paths follow the current track order."
        }
    },
    "required": [
        "track",
        "clip"
    ],
    "type": "object"
}
```

## transport.seek

Set the song playhead position in ticks.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "ticks": {
            "description": "The ticks argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "ticks"
    ],
    "type": "object"
}
```

## transport.setLoopRange

Set and enable the song loop range in bars.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "endBar": {
            "description": "The endBar argument for this command.",
            "type": "integer"
        },
        "startBar": {
            "description": "The startBar argument for this command.",
            "type": "integer"
        }
    },
    "required": [
        "startBar",
        "endBar"
    ],
    "type": "object"
}
```

## transport.setPosition

Set the song playhead position.

```json
{
    "additionalProperties": false,
    "properties": {
        "bar": {
            "description": "The bar argument for this command.",
            "type": "integer"
        },
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        },
        "ticks": {
            "description": "The ticks argument for this command.",
            "type": "integer"
        }
    },
    "type": "object"
}
```

## transport.stop

Stop playback.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        }
    },
    "type": "object"
}
```

## transport.togglePause

Toggle playback pause state.

```json
{
    "additionalProperties": false,
    "properties": {
        "dryRun": {
            "description": "Validate and preview the change without committing project or history changes.",
            "type": "boolean"
        }
    },
    "type": "object"
}
```
