# Header

| Field | Type | Description |
| ----- | ---- | ----------- |
| Type | uint8 | Type of the message |
| Flags | uint8 | Flags |
| Length | uint16 | Length of the message |
| Object ID | uint32 | ID of the object |

If object ID is NULL, the message is generic message

| Packet ID | Packet Name | Packet Description | Direction |
| --------- | ----------- | ------------------ | --------- |
| 0x00 | Client Hello | Client Hello, sent as first packet | Serverbound |
| 0x01 | Server Hello | Sent as response to Client Hello, sent only if the client connection is accepted | Clientbound |
| 0x02 | Disconnect | Disconnection notice | Clientbound |
| 0x03 | Notice | Message, client is disconnected immediately if it's is a error | Clientbound |
| 0x04 | Start | Start session | Clientbound |
| 0x05 | environment() | Sent when new environment is added (or on before Start) | Clientbound |
| 0x06 | environment.changed() | Updates environment info | Clientbound |
| 0x07 | environment.mascot() | Adds a new mascot to the environment | Clientbound |
| 0x08 | environment.withdrawn() | Removes the environment | Clientbound |
| 0x09 | prototype() | Sent when new prototype is added (or on before Start) | Clientbound |
| 0x0A | prototype.name() | Sets name of prototype referred by Object ID | Clientbound |
| 0x0B | prototype.display_name() | Sets display name of prototype referred by Object ID | Clientbound |
| 0x0C | prototype.path() | Sets path of prototype referred by Object ID | Clientbound |
| 0x0D | prototype.fd() | Sets file descriptor pointing to prototype dir (O_PATH | O_DIRECTORY) | Clientbound |
| 0x0E | prototype.add_action() | Adds a new action to the prototype referred by Object ID | Clientbound |
| 0x0F | prototype.add_behavior() | Adds a new behavior to the prototype referred by Object ID | Clientbound |
| 0x10 | prototype.icon() | Sets icon of prototype referred by Object ID | Clientbound |
| 0x11 | prototype.author() | Sets author of prototype referred by Object ID | Clientbound |
| 0x12 | prototype.version() | Sets version of prototype referred by Object ID | Clientbound |
| 0x13 | Commit prototypes | Apply changes to prototypes (atomically) | Clientbound |
| 0x14 | mascot.migrated() | Migrates mascot to the new environment | Clientbound |
| 0x15 | mascot.disposed() | Disposes mascot | Clientbound |
| 0x16 | mascot->get_info() | Gets information about mascot | Serverbound |
| 0x17 | mascot.info() | Contains current state of the mascot | Clientbound |
| 0x18 | mascot.clicked() | Triggered on secondary button click on mascot, creates new Object of type click_event | Clientbound |
| 0x19 | plugin() | Announces new plugin | Clientbound |
| 0x1A | plugin.description() | Contains full description of the plugin | Clientbound |
| 0x1B | plugin.window() | Announces new plugin-managed window | Clientbound |
| 0x1C | plugin.unloaded() | Unloads plugin | Clientbound |
| 0x1D | Commit plugins | Apply changes to plugins (atomically) | Clientbound |
| 0x1E | Select | Initializes selection, creates new Object of type selection | Serverbound |
| 0x1F | selection.done() | Selection completed, contains information about the selected point | Clientbound |
| 0x20 | selection.cancel() | Selection cancelled, for example by explicit request or if another selection is started | Clientbound |
| 0x21 | Reload prototype | Reloads prototype by its path, if path is empty, reloads all prototypes | Serverbound |
| 0x22 | Import prototype | Imports prototype by file descriptor, creates new Object of type import | Clientbound |
| 0x23 | import.failed() | Import failed, can be sent if prototype with same name already exists, then import can be retried with replace flag, acts as destructor for object | Clientbound |
| 0x24 | import.started() | Import started | Clientbound |
| 0x25 | import.finished() | Import finished, acts as destructor for object | Clientbound |
| 0x26 | import.progress() | Import progress | Clientbound |
| 0x27 | Export | Exports prototype by its id, creates new Object of type export | Clientbound |
| 0x28 | export.failed() | Export failed, acts as destructor for object | Clientbound |
| 0x2A | export.finished() | Export finished, acts as destructor for object | Clientbound |
| 0x2B | Spawn | Summons mascot | Serverbound |
| 0x2C | Dispose | Disposes mascot referred by its id | Serverbound |
| 0x2E | environment->close() | Asks environment to close | Serverbound |
| 0x2F | plugin->set_policy() | Sets policy for plugin | Serverbound |
| 0x30 | plugin->restore_windows() | Moves windows to center of screen if possible | Serverbound |
| 0x3A | plugin->deactivate() | Deactivates plugin | Serverbound |
| 0x3B | plugin->activate() | Activates plugin | Serverbound |
| 0x3C | plugin.status() | Status of plugin changed | Clientbound |
| 0x3D | selection->cancel() | Cancels selection | Serverbound |
| 0x3E | Share shm pools | Shares shared memory pools, creates new Object of type shm_pool | Serverbound |
| 0x3F | shm_pool->create_buffer() | Creates buffer in shared memory pool, returns new Object of type buffer | Serverbound |
| 0x40 | shm_pool.imported() | Server imported shared memory pool successfully | Clientbound |
| 0x41 | shm_pool.failed() | Server failed to import shared memory pool | Clientbound |
| 0x42 | shm_pool->destroy() | Destroys shared memory pool referenced by Object ID | Serverbound |
| 0x43 | buffer->destroy() | Destroys buffer referenced by Object ID | Serverbound |
| 0x44 | click_event->accept() | Opens popup menu at click position, creates new Object of type popup, acts as destructor for object | Serverbound |
| 0x45 | click_event->ignore() | Destroys click event referenced by Object ID | Serverbound |
| 0x46 | popup->child_popup() | Creates child popup menu at specified position, creates new Object of type popup | Serverbound |
| 0x47 | popup->attach() | Attaches buffer to popup and immediately renders it | Serverbound |
| 0x48 | popup.mapped() | Sent when popup is mapped | Clientbound |
| 0x49 | popup.unmapped() | Sent when popup is unmapped | Clientbound |
| 0x4A | popup.dismissed() | Sent when popup is dismissed, acts as destructor for object | Clientbound |
| 0x4B | popup.enter() | Sent when mouse enters popup | Clientbound |
| 0x4C | popup.leave() | Sent when mouse exits popup | Clientbound |
| 0x4D | popup.motion() | Sent when mouse moves within popup | Clientbound |
| 0x4E | popup.clicked() | Sent when mouse clicks popup | Clientbound |
| 0x4F | popup->dismiss() | Sent when popup is discarded, acts as destructor for object | Serverbound |
| 0x50 | Apply behavior | Applies behavior to mascot | Serverbound |
| 0x51 | Get config key | Gets config value by key | Serverbound |
| 0x52 | Set config key | Sets config value by key | Serverbound |
| 0x53 | List config keys | Lists all config keys | Serverbound |
| 0x54 | Config key | Sends config key along with value, sent if client calls get, set or list (sent for each key in that case), clients should expect new keys at any time | Clientbound |
| 0x55 | click_event.expired() | Sent when click event expires, for example if new click event is created or some client is accepted click event| Clientbound |
| 0x56 | Stop | Ask overlay to stop | Serverbound |
| 0x57 | prototype->withdraw() | Sent when prototype is withdrawn, acts as destructor for object | Serverbound |


# Client Hello - 0x00

Should be first message sent by client to server, is protocol violation to sent any other message before this one. If sent at any other time, event is ignored.

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Version | uint64 | Version of protocol | |

# Server Hello - 0x01

Should be first message sent by server to client, is protocol violation to sent any other message before this one. If sent at any other time, event is ignored.

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Version | uint64 | Version of protocol | |

# Disconnect - 0x02

Sent by server when it decides to disconnect client, message doesn't contain any additional information.


# Notice - 0x03

Sent by server when it wants to notify client about something, can be anything starting from Informational messages, Warning messages to Error messages. All error messages are fatal and client will be disconnected without any further notice.

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Severity | uint8 | Severity of notice | |
| Alert | uint8 | Used to signal GUI clients to show message as alert | |
| Message | string | Message to be displayed | Can be localized string such as "generic.some.kind.of.notice" |
| Values | uint8_t | Count of values that is used for formatting message | |
| Value | string[] | Values used for message formatting, its up to client how to use them | Can be localized string such as "generic.some.kind.of.notice" |

Severity:
 - 0x00 INFO - Informational message
 - 0x01 WARN - Warning message, sent during runtime errors or when something goes wrong on overlay
 - 0x02 ERROR - Error message, fatal errors, For example sent if proxy initialization failed


# Start - 0x04

Notices that initialization is complete and client is knows enough to communicate with server.

This message contains no additional information.


# environment() - 0x05

Environment announcement. Sent when new display is added, contains information about new display

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Id | uint32 | ID of new environment Object | Should be treated as opaque ID |
| Name | string | Name of display | IF AVAILABLE |
| Description | string | Description of display | IF AVAILABLE |
| Compositor X | uint32 | X coordinate in compositor space | |
| Compositor Y | uint32 | Y coordinate in compositor space | |
| Width | uint32 | Logical width of the display | |
| Height | uint32 | Logical height of the display | |
| Scale | float | Logical scale of the display | |

# environment.changed() - 0x06

Environment properties changed. Sent when display properties changed, contains information about new display properties

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Name | string | Name of display | IF AVAILABLE |
| Description | string | Description of display | IF AVAILABLE |
| Compositor X | uint32 | X coordinate in compositor space | |
| Compositor Y | uint32 | Y coordinate in compositor space | |
| Width | uint32 | Logical width of the display | |
| Height | uint32 | Logical height of the display | |
| Scale | float | Logical scale of the display | |

# environment.mascot() - 0x07

Mascot created inside environment.

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| ID | uint32 | Object ID of the new mascot | Should be treated as opaque ID |
| Prototype id | uint32 | Object ID of the prototype | Should be treated as opaque ID |

# environment.withdrawn() - 0x08

Environment withdrawn. Sent when environment is destroyed, for example when display is removed or by explicit request from client

Object ID is used to identify environment that was withdrawn.
If some mascots are present, they will be migrated to another environment or destroyed before that packet.

# prototype() - 0x09

Announcement of new mascot prototype. When received, prototype should not be used until it's commited by **Commit prototypes** packet

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| ID | uint32 | Object ID of the new mascot prototype | Should be treated as opaque ID |

# prototype.name() - 0x0A

Name of the mascot prototype. Sent when prototype is created or name is changed

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Name | string | Name of the prototype | |

# prototype.display_name() - 0x0B

Name of the mascot prototype's display. Sent when prototype is created or display name is changed

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Name | string | Name of the prototype's display | |

# prototype.path() - 0x0C

Relative path of the mascot, where full path known only to overlay.

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Path | string | Relative path of the mascot | |

# prototype.fd() - 0x0D

File descriptor that points to the mascot prototype dir. Opened with O_DIRECTORY | O_RDONLY | O_PATH flags.

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| FD | SCM_RIGHTS | File descriptor of the prototype | |

# prototype.add_action() - 0x0E

Add action name to the mascot prototype. Exposes action names available in the prototype.

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Action | string | Action name | |
| Type | uint32 | Type of the action | |
| Embedded type | uint32 | Type of the embedded action | |
| Border type | uint32 | Type of the border used for the action | |

# prototype.add_behavior() - 0x0F

Add behavior name to the mascot prototype. Exposes behavior names available in the prototype.

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Behavior | string | Behavior name | |
| Condition | uint8 | If behavior is condition container | |
| Hidden | uint8 | If behavior should be hidden from user | |
| Frequency | uint64 | Frequency of the behavior | |

# prototype.icon() - 0x10

Relative path from fd to sprite that should be used as icon.

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Icon | string | Relative path to the icon sprite | |

# prototype.author() - 0x11

Name of the author of the mascot.

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Author | string | Name of the author | |

# prototype.version() - 0x12

Version of the mascot.

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Version | uint32 | Version of the mascot | |

# Commit prototypes - 0x13

Commit added/removed prototypes. Client should add new prototypes to it's list of known prototypes.
This packet has no payload.

# mascot.migrated() - 0x14

Mascot migrates from one environment to another. Can be caused by movement, dropping or if parent environment is withdrawn.

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Environment | uint32 | Object ID of new environment | |

# mascot.disposed() - 0x15

Mascot is disposed. Sent when mascot is **fully** disposed, that means even if dispose animation is available and dispose animations are enabled, it will be sent once mascot is removed.

Object ID is used as argument.

# mascot->get_info() - 0x16

Ask server to provide information about mascot. If mascot is not exists, server will issue a warning.
It's not guaranteed when server will provide information about mascot. Its recommended to call this method before any manipulations with mascot, as transformations are not reported to clients.

Object ID is used as argument.

# mascot.info() - 0x17

Server sents information about mascot. Mascot that receives that information is reffered by Object ID

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Prototype ID | uint32 | Object ID of the prototype | |
| Environment ID | uint32 | Object ID of the environment | |
| Referenced Window ID | uint32 | Object ID of the window that mascot is attached to | Attached means here "mascot is on border of window or holds it" |
| Current state | uint32 | Current state of mascot | |
| Current action name | string | Name of current action | |
| Current action index | uint32 | Index of current action | |
| Current behavior name | string | Name of current behavior | |
| Current affordance | string | Name of current affordance | |
| Actions stack depth | uint16 | Number of actions in stack | Can be used in debug purposes |
| Actions | tuple<string, uint32>[] | List of pairs of string and uint32, representing action name and action index | |
| Behavior pool size | uint32 | Number of behaviors in pool | |
| Behavior pool | tuple<string, uint64>[] | List of pairs of string and uint64, representing behavior name and it's frequency| |
| Variables count | uint8 | Number of variables in mascot | Currently hardcoded to 128, however not all of them are used |
| Variables | variable[] | List of variables | Refer to types section for actual variable type reference |


# mascot.clicked() - 0x18

Sent when user clicks on mascot using secondary mouse button. Creates new click_event Object ID and revokes previous one (if not currently in use)

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Object ID | uint32 | Object ID of the new click_event | |


TODO:
# plugin() - 0x19 | Announce new plugin
# plugin.description() - 0x1A | Reports plugin description
# plugin.window() - 0x1B | Announce new window managed by plugin
# plugin.unloaded() - 0x1C | Reports plugin unload
# Commit plugin - 0x1D | Informs client about end of plugin changes

# Select - 0x1E

Initializes selection operation, creates new selection object and cancels any unfinished old selection objects

| Field | Type | Description | Notes |
|-------|------|-------------|-------|
| Object ID | uint32 | Object ID of the new selection to be created | |
| Environments count | uint8 | Number of participating environments in selection | |
| Environments | uint32[] | List of environments IDs in selection | |


# selection.done() - 0x1F

User selected spot on the screen

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| Environment ID | uint32 | Environment ID where selection was done | |
| Mascot ID | uint32 | ID of the selected mascot | |
| X | uint32 | X coordinate of the selected spot | |
| Y | uint32 | Y coordinate of the selected spot | |
| Surface local X | uint32 | X coordinate of the selected spot relative to the surface | Only makes sense when Mascot ID is not 0 |
| Surface local Y | uint32 | Y coordinate of the selected spot relative to the surface | Only makes sense when Mascot ID is not 0 |

# selection.cancel() - 0x20

User canceled selection operation. Object ID field in header used to identify selection to be canceled

# Reload prototype - 0x21

Reload prototype referenced by name or reload all prototypes if name was empty. Server will respond with Notice or Warning to that message.

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| Name | string | Name of the prototype to be reloaded | Empty string means reload all prototypes |

# Import prototype - 0x22

Start import operation of prototype from file. File descriptor should point to either directory that overlay will be able to open or zipfile. Insides in all cases should be in .wlshm mascot format.

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| Object ID | uint32 | ID that will be created for import Object | |
| FD | SCM_RIGHTS | File descriptor pointing to the file descriptor of the prototype | |

Flags from header is used here
0x01 is treated as REPLACE flag, in case if prototype already exists

# import.failed() - 0x23

Import operation is failed. Object ID referenced in header will be destroyed.
Explaination will be sent in Notice Warning with Alert = 1 right after that message.

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| Error | uint32 | Error message code | |

Error:
  - 0: Unknown error
  - 1: Invalid file format
  - 3: Referenced mascot name already exists and REPLACE is not set, Alert in Notice is set to 0 in that case
  - 4: File is not a valid tar file
  - 6: File is not file
  - 7: File is not opened in right mode (Expected read or readwrite)

# import.started() - 0x24

Informs client that import is started for Object ID referenced in header.

# import.finished() - 0x25

Import is successful. Object ID referenced in header will be destroyed.
It is recommended to reload prototype after import.

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| Relative path | string | Relative path to the imported prototype | |

# import.progress() - 0x26

Informs client about progress of import operation. Take into account that this message may be never sent, in that case GUI clients may want to show progress bar with indefinite progress or not at all.

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| Progress | float | Progress percentage | 0.0 - 1.0|

# Export - 0x27

Export prototype referenced by Object ID to fd. Packet will create new export Object of type export.

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| FD | SCM_RIGHTS | File descriptor opened in WRITE mode | |
| Object ID | uint32 | New ID for export Object | |
| Prototype ID | uint32 | ID of prototype to export | |

# export.failed() - 0x28

Export operation is failed. Object ID referenced in header will be destroyed.
Explaination will be sent in Notice Warning with Alert = 1 right after that message.

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| Error | uint32 | Error message code | |

Error:
  - 0: Unknown error
  - 1: FD is not valid
  - 2: FD not opened in WRITE mode
  - 3: Referenced prototype ID is not valid
  - 4: Server errored

# export.finished() - 0x29

Export operation is successful. Object ID referenced in header will be destroyed.

# Spawn - 0x2A

Spawn mascot by prototype ID. Any errors reported via Notice Warning with Alert = 1.

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| Prototype ID | uint32 | ID of prototype to spawn | |
| Environment ID | uint32 | ID of environment to spawn in | |
| X | uint32 | X coordinate | |
| Y | uint32 | Y coordinate | |
| Spawn behavior | string | Spawn behavior | Can be empty for default behavior |

# Dispose - 0x2B

Dispose mascot by Object ID. Will cause dispose event on overlay, but if mascot has dispose animation and dispose animations are enabled, there's no guarantee that the mascot will be disposed immediately.
Any errors reported via Notice Warning with Alert = 1.

Object ID referenced in header used as mascot ID.

# environment->close()

Suggest environment to close. Acts same as display disconnect. NOTICE: If environment is closed, it not possible to later reopen it, only possible workaround is to reconnect display.

Object ID referenced in header used as environment ID.

Any errors reported via Notice Warning with Alert = 1.

TODO:
plugin->set_policy()
plugin->restore_windows()
plugin->deactivate()
plugin->activate()
plugin.status()
window->*
window.*

# selection->cancel() - 0x3C

Cancel selection **caused by client**.

Object ID referenced in header used as selection ID.

Any errors reported via Notice Warning with Alert = 1.

TODO: shm_pools
TODO: buffers

# click_event->accept() - 0x44
TODO

TODO: popups

# Apply behavior - 0x50

Apply behavior by name to mascot referenced by Object ID. Will cause apply behavior event on overlay.

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| Behavior | string | Behavior to apply | |

# Get config key - 0x51

Schedules callback to get config key value.

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| Key | string | Config key to get value for | |

# Set config key - 0x52

Sets config key value, schedules callback to get updated value

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| Key | string | Config key to set value for | |
| Value | string | Value to set | Will be converted on server to appropriate type |

# List config keys - 0x53

Lists all config keys.


# Config key - 0x54

Get config key value.

| Field | Type | Description | Note |
|-------|------|-------------|-------|
| Key | string | Config key to get value for | |
| Value | string | Value to get | Note: Client should present values as appropriate types |

# click_event.expired() - 0x55

Sent when click event is invalidated by any cause.

Object ID referenced in header used as click event ID.
