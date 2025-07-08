from __future__ import annotations
import os
import struct
import socket
from typing import Callable, Any
from dataclasses import dataclass

import random

prototypes: dict[int, Prototype] = {}
environments: dict[int, Environment] = {}
mascots: dict[int, Mascot] = {}
exports: dict[int, Export] = {}
imports: dict[int, Import] = {}
objects: dict[int, Any] = {}
active_selection: Selection | None = None

class Mascot:
    id: int
    prototype: Prototype
    environment: Environment
    current_action: str
    current_action_index: int
    current_state: int
    current_behavior: str
    affordance: str
    action_stack: list[tuple[str,int]]
    behavior_pool: list[tuple[str,int]]
    variables: list[MascotInfo.Variable]

    socket: socket.socket

    def __init__(self, id: int, prototype: Prototype, socket: socket.socket):
        self.id = id
        self.socket = socket

        self.prototype = prototype
        self.environment = None
        self.current_action = ''
        self.current_action_index = -1
        self.current_behavior = ''
        self.affordance = ''
        self.action_stack = []
        self.behavior_pool = []
        self.variables = []

    def dismiss(self):
        packet = Dispose(self.id)
        self.socket.send(packet.encode())

    def migrated(self, packet: MascotMigrated):
        environment = environments.get(packet.environment_id)
        self.environment = environment
        if environment:
            self.environment.mascots[self.id] = self

    def disposed(self, packet: MascotDisposed):
        self.environment.mascots.pop(self.id, None)
        self.environment = None

    def info(self, packet: MascotInfo):
        self.prototype = prototypes.get(packet.prototype_id)
        self.environment = environments.get(packet.environment_id)
        self.current_action = packet.current_action_name
        self.current_action_index = packet.current_action_index
        self.current_state = packet.current_state
        self.current_behavior = packet.current_behavior_name
        self.affordance = packet.current_affordance_name
        self.action_stack = packet.actions
        self.behavior_pool = packet.behaviors
        self.variables = packet.variables

class Environment:
    id: int
    name: str
    description: str
    x: int
    y: int
    width: int
    height: int
    scale: float

    mascots: dict[int, Mascot]

    socket: socket.socket

    def __init__(self, id: int, socket: socket.socket, *args):
        self.id = id
        self.socket = socket
        self.mascots = {}

        self.change(*args)

    def change(self, name, description, x, y, width, height, scale):
        self.name = name
        self.description = description
        self.x = x
        self.y = y
        self.width = width
        self.height = height
        self.scale = scale

    def mascot_added(self, packet: EnvironmentMascot):
        prototype = prototypes.get(packet.prototype_id)
        mascot = Mascot(packet.mascot_new_id, prototype, self.socket)
        self.mascots[mascot.id] = mascot
        mascots[mascot.id] = mascot
        objects[mascot.id] = mascot
        mascot.environment = self

    def close(self):
        packet = EnvironmentClose(self.id)
        self.socket.send(packet.encode())

    def withdraw(self, EnvironmentWithdrawn):
        environments.pop(self.id)
        objects.pop(self.id)

    def __repr__(self):
        return \
        f"Environment(id={self.id}, name={self.name}, description={self.description}, pos=({self.x},{self.y}), size=({self.width},{self.height}), scale={self.scale}, with {len(self.mascots)} mascots)"

class Prototype:
    id: int
    name: str
    display_name: str
    path: str
    fd: int
    actions: list[str]
    behaviors: list[str]
    icon_fd: int
    author: str = ''
    version: int = 0

    socket: socket.socket

    def __init__(self, id: int, socket: socket.socket):
        self.id = id
        self.name = ''
        self.display_name = ''
        self.path = ''
        self.fd = -1
        self.icon_fd = -1
        self.actions = []
        self.behaviors = []
        self.author = ''
        self.version = 0

    def withdrawn(self, packet: PrototypeWithdraw):
        objects.pop(self.id)
        prototypes.pop(self.id)

    def __del__(self):
        if (self.fd != -1):
            os.close(self.fd)
        if (self.icon_fd != -1):
            os.close(self.icon_fd)

class Selection:
    id: int
    environments: list[Environment]

    socket: socket.socket

    def __init__(self, environments: list[Environment], socket: socket.socket):
        global active_selection
        self.id = random.randint(0, 2**24) | (5 << 24)
        self.environments = environments

        self.socket = socket

        packet = Select(self.id, [env.id for env in environments])
        socket.send(packet.encode())

        objects[self.id] = self
        active_selection = self

    def selected(self, packet: SelectionDone):
        global active_selection
        environment = environments.get(packet.environment_id)
        mascot = mascots.get(packet.mascot_id)
        logger.info(f"Selected point ({packet.x},{packet.y}) at {environment}, underlying mascot {mascot}")
        if active_selection == self:
            active_selection = None

    def cancelled(self, packet: SelectionCancelled):
        global active_selection
        logger.info(f"Selection cancelled")
        if active_selection == self:
            active_selection = None

    def cancel(self):
        packet = SelectionCancel(self.id)
        self.socket.send(packet.encode())

    def __del__(self):
        global active_selection
        objects.pop(self.id, None)
        if active_selection == self:
            active_selection = None

class Export:
    id: int
    export_fd: int
    prototype: Prototype

    socket: socket.socket
    def __init__(self, export_fd: int, prototype: Prototype, sock: socket.socket):
        self.export_fd = export_fd
        self.prototype = prototype
        self.socket = sock
        self.id = random.randint(0, 2**24) | (7 << 24)

        packet = ExportPrototype(self.id, self.export_fd, self.prototype.id)
        buf = packet.encode()

        socket.send_fds(self.socket, [buf], packet.fds)

        objects[self.id] = self
        exports[self.id] = self

    def finished(self, packet: ExportFinished):
        objects.pop(self.id, None)
        exports.pop(self.id, None)

    def failed(self, packet: ExportFailed):
        objects.pop(self.id, None)
        exports.pop(self.id, None)

class Import:
    id: int
    import_fd: int

    socket: socket.socket

    def __init__(self, import_fd: int, force: bool, sock: socket.socket):
        self.import_fd = import_fd
        self.force = force
        self.socket = sock

        self.id = random.randint(0, 2**24) | (6 << 24)
        packet = ImportPrototype(self.id, self.force, self.import_fd)
        buf = packet.encode()

        socket.send_fds(self.socket, [buf], packet.fds)

        imports[self.id] = self
        objects[self.id] = self

    def started(self, packet: ImportStarted):
        pass

    def failed(self, packet: ImportFailed):
        pass

    def finished(self, packet: ImportFinished):
        pass

    def progress(self, packet: ImportProgress):
        pass

# --------------------------

class Packet:

    buffer: bytes
    read_position: int = 0
    fds: list[int]
    fd_readposition: int = 0

    type: int
    flags: int
    object_id: int = 0
    length: int = 0

    def __init__(self, type: int, flags: int, object_id: int, buffer: bytes):
        self.type = type
        self.flags = flags
        self.object_id = object_id
        self.buffer = buffer or b''
        self.fds = []


    def serialize(self) -> bytes:
        return b''

    def encode(self) -> bytes:
        self.buffer = self.serialize()
        return struct.pack('bbHI', self.type, self.flags, 8 + len(self.buffer), self.object_id) + self.buffer

    def decode(self):
        self.type, self.flags, self.length, self.object_id = struct.unpack('bbHI', self.buffer[:8])
        self.read_position += 8
        self.deserialize()

    def deserialize(self) -> None:
        ...

    def read_values(self, signature: str) -> list[Any]:
        signature = "=" + signature
        length = struct.calcsize(signature)
        values = struct.unpack(signature, self.buffer[self.read_position:self.read_position + length])
        self.read_position += length
        return list(values)

    def write_values(self, signature: str, *values: Any) -> None:
        self.buffer += struct.pack(signature, *values)

    def consume_fd(self) -> int:
        fd = self.fds[self.fd_readposition]
        self.fd_readposition += 1
        return fd

    def add_fd(self, fd: int) -> None:
        self.fds.append(os.dup(fd))
        self.fd_readposition += 1

    def __del__(self):
        for fd in self.fds:
            os.close(fd)

class ClientHello(Packet):

    type: int = 0
    version: int = 0

    def __init__(self, version: int):
        self.version = version
        Packet.__init__(self, self.type, 0, 0, b'')

    def serialize(self) -> bytes:
        return struct.pack('Q', self.version)

class ServerHello(Packet): ...
class Disconnect(Packet): ...
class Notice(Packet):
    severity: int
    alert: int
    message: str
    formating_values: list[str]

    def __init__(self, *args):
        Packet.__init__(self, *args)
        self.formating_values = []

    def deserialize(self):
        self.severity, self.alert, message_length = self.read_values("bbb")
        self.message = self.read_values(f"{message_length}s")[0].decode()
        formating_values_count = self.read_values("b")[0]
        for _ in range(formating_values_count):
            value_length = self.read_values("b")[0]
            value = self.read_values(f"{value_length}s")[0]
            self.formating_values.append(value.decode())


class StartSession(Packet): ...
class EnvironmentAnnouncement(Packet):
    new_id: int
    name: str
    description: str
    x: int
    y: int
    width: int
    height: int
    scale: float

    def deserialize(self):
        self.new_id, name_len = self.read_values("Ib")
        self.name, description_len = self.read_values(f"{name_len}sb")
        self.description, self.x, self.y, self.width, self.height, self.scale = self.read_values(f"{description_len}sIIIIf")
        self.name = self.name.decode('utf-8')
        self.description = self.description.decode('utf-8')

class EnvironmentChanged(Packet):
    name: str
    description: str
    x: int
    y: int
    width: int
    height: int
    scale: float

    def deserialize(self):
        name_len = self.read_values("b")[0]
        self.name, description_len = self.read_values(f"{name_len}sb")
        self.description, self.x, self.y, self.width, self.height, self.scale = self.read_values(f"{description_len}sIIIIf")
        self.name = self.name.decode('utf-8')
        self.description = self.description.decode('utf-8')

class EnvironmentMascot(Packet):
    mascot_new_id: int
    prototype_id: int

    def deserialize(self) -> None:
        self.mascot_new_id, self.prototype_id = self.read_values("II")

class EnvironmentWithdrawn(Packet): ...

class StartPrototype(Packet):
    prototype_id: int

    def deserialize(self) -> None:
        self.prototype_id = self.read_values("I")[0]

class PrototypeName(Packet):
    name: str

    def deserialize(self) -> None:
        name_len = self.read_values("b")[0]
        self.name = self.read_values(f"{name_len}s")[0].decode()

class PrototypeDisplayName(Packet):
    display_name: str

    def deserialize(self) -> None:
        name_len = self.read_values("b")[0]
        self.display_name = self.read_values(f"{name_len}s")[0].decode()

class PrototypePath(Packet):
    path: str

    def deserialize(self) -> None:
        path_len = self.read_values("b")[0]
        self.path = self.read_values(f"{path_len}s")[0].decode()

class PrototypeFD(Packet):
    fd: int

    def deserialize(self) -> None:
        self.fd = self.consume_fd()

class PrototypeAddAction(Packet):
    action: str

    def deserialize(self) -> None:
        action_len = self.read_values("b")[0]
        self.action = self.read_values(f"{action_len}s")[0].decode()

class PrototypeAddBehavior(Packet):
    behavior: str

    def deserialize(self) -> None:
        behavior_len = self.read_values("b")[0]
        self.behavior = self.read_values(f"{behavior_len}s")[0].decode()

class PrototypeIcon(Packet):
    iconfd: int

    def deserialize(self) -> None:
        self.iconfd = self.consume_fd()

class PrototypeAuthor(Packet):
    author: str

    def deserialize(self) -> None:
        author_len = self.read_values("b")[0]
        self.author = self.read_values(f"{author_len}s")[0].decode()

class PrototypeVersion(Packet):
    version: int

    def deserialize(self) -> None:
        self.version = self.read_values("Q")[0]

class CommitPrototypes(Packet): ...

class MascotMigrated(Packet):
    environment_id: int

    def deserialize(self) -> None:
        self.environment_id = self.read_values("I")[0]

class MascotDisposed(Packet): ...

class MascotGetInfo(Packet):
    type: int = 0x16

    def __init__(self, mascot_id: int):
        Packet.__init__(self, self.type, 0, mascot_id, b'')

class MascotInfo(Packet):

    @dataclass
    class Variable:
        kind: int
        value: int | float
        used: bool
        evaulate_once: bool
        script_id: int

    prototype_id: int
    environment_id: int
    current_state: int
    current_action_name: str
    current_action_index: int
    current_behavior_name: str
    current_affordance_name: str
    actions: list[tuple[str, int]]
    behaviors: list[tuple[str, int]]
    variables: list[Variable]

    def __init__(self, type: int, flags: int, object_id: int, buffer: bytes):
        Packet.__init__(self, type, flags, object_id, buffer)
        self.actions = []
        self.behaviors = []
        self.variables = []

    def deserialize(self):
        self.prototype_id, self.environment_id,\
        self.current_state, cur_act_len = self.read_values("IIIB")
        self.current_action_name, self.current_action_index, cur_beh_len = self.read_values(f"{cur_act_len}sHB")
        self.current_behavior_name, affordance_len = self.read_values(f"{cur_beh_len}sB")
        self.current_affordance_name, action_pool_len = self.read_values(f"{affordance_len}sB")
        for _ in range(action_pool_len):
            action_name_len = self.read_values("B")[0]
            action_name, action_index = self.read_values(f"{action_name_len}sI")
            self.actions.append((action_name, action_index))
        behavior_pool_len = self.read_values("B")[0]
        for _ in range(behavior_pool_len):
            behavior_name_len = self.read_values("B")[0]
            behavior_name, behavior_frequency = self.read_values(f"{behavior_name_len}sQ")
            self.behaviors.append((behavior_name, behavior_frequency))
        variables_count = self.read_values("H")[0]
        for _ in range(variables_count):
            kind = self.read_values("B")[0]
            value, used, evaluated_once, script_id = self.read_values(f"{'i' if kind != 1 else 'f'}??H")
            self.variables.append(MascotInfo.Variable(kind, value, used, evaluated_once, script_id))

class MascotClicked(Packet):
    new_clicked_id: int

    def deserialize(self):
        self.new_clicked_id = self.read_values("I")[0]


class Select(Packet):
    type: int = 0x1E
    new_selected_id: int
    environments: list[int]

    def __init__(self, id: int, env_ids: list[int]):
        Packet.__init__(self, self.type, 0, 0, b'')
        self.new_selected_id = id
        self.environments = env_ids

    def serialize(self) -> bytes:
        return struct.pack("IB" + "I" * len(self.environments), *([self.new_selected_id, len(self.environments)] + self.environments))

class SelectionDone(Packet):
    environment_id: int
    mascot_id: int
    x: int
    y: int
    surface_x: int
    surface_y: int

    def deserialize(self) -> None:
        self.environment_id, self.mascot_id, self.x, self.y, self.surface_x, self.surface_y = self.read_values("IIIIII")

class SelectionCancelled(Packet): ...

class ReloadPrototype(Packet):
    prototype_path: str
    type: int = 0x21

    def __init__(self, prototype_path: str):
        Packet.__init__(self, self.type, 0, 0, b'')
        self.prototype_path = prototype_path

    def serialize(self):
        return struct.pack(f"b{len(self.prototype_path)}s", len(self.prototype_path), self.prototype_path.encode("utf-8"))

class ImportPrototype(Packet):
    new_object_id: int
    prototype_fd: int
    type: int = 0x22

    def __init__(self, new_object_id: int, force: bool, prototype_fd: int):
        Packet.__init__(self, self.type, force, 0, b'')
        self.new_object_id = new_object_id
        self.prototype_fd = prototype_fd

    def serialize(self):
        self.add_fd(self.prototype_fd)
        return struct.pack("I", self.new_object_id)

class ImportFailed(Packet):
    error_code: int

    def deserialize(self):
        self.error_code = self.read_values("i")[0]

class ImportStarted(Packet): ...
class ImportFinished(Packet):
    relative_path: str

    def deserialize(self) -> None:
        relative_path_len = self.read_values(f"b")[0]
        self.relative_path = self.read_values(f"{relative_path_len}s")[0].decode()

class ImportProgress(Packet):
    progress: float

    def deserialize(self) -> None:
        self.progress = self.read_values("f")[0]

class ExportPrototype(Packet):
    export_fd: int
    new_object_id: int
    prototype_id: int
    type: int = 0x27

    def __init__(self, object_id: int, export_fd: int, prototype_id: int):
        Packet.__init__(self, self.type, 0, 0, b'')
        self.new_object_id = object_id
        self.export_fd = export_fd
        self.prototype_id = prototype_id

    def serialize(self):
        self.add_fd(self.export_fd)
        return struct.pack("II", self.new_object_id, self.prototype_id)

class ExportFailed(Packet):
    error_code: int

    def deserialize(self) -> None:
        self.error_code = self.read_values("I")[0]

class ExportFinished(Packet): ...

class Spawn(Packet):
    prototype_id: int
    environment_id: int
    x: int
    y: int
    spawn_behavior: str

    type: int = 0x2A

    def __init__(self, prototype_id: int, environment_id: int, x: int, y: int, spawn_behavior: str):
        Packet.__init__(self, self.type, 0, 0, b'')
        self.prototype_id = prototype_id
        self.environment_id = environment_id
        self.x = x
        self.y = y
        self.spawn_behavior = spawn_behavior

    def serialize(self) -> bytes:
        return struct.pack(f"IIIIb{len(self.spawn_behavior)}s", self.prototype_id, self.environment_id, self.x, self.y, len(self.spawn_behavior), self.spawn_behavior.encode())

class Dispose(Packet):
    type: int = 0x2B

    def __init__(self, id: int):
        Packet.__init__(self, self.type, 0, id, b'')

class EnvironmentClose(Packet):
    type: int = 0x2E

    def __init__(self, id: int):
        Packet.__init__(self, self.type, 0, id, b'')

class SelectionCancel(Packet):
    type: int = 0x3C

    def __init__(self, id: int):
        Packet.__init__(self, self.type, 0, id, b'')

class ApplyBehavior(Packet):
    type: int = 0x50
    behavior: str

    def __init__(self, id: int, behavior: str):
        Packet.__init__(self, self.type, 0, id, b'')
        self.behavior = behavior

    def serialize(self) -> bytes:
        return struct.pack(f"b{len(self.behavior)}s", len(self.behavior), self.behavior.encode())

class GetConfigKey(Packet):
    type: int = 0x51
    key: str

    def __init__(self, key: str):
        Packet.__init__(self, self.type, 0, 0, b'')
        self.key = key

    def serialize(self) -> bytes:
        return struct.pack(f"b{len(self.key)}s", len(self.key), self.key.encode())

class SetConfigKey(Packet):
    type: int = 0x52
    key: str
    value: str

    def __init__(self, key: str, value: str):
        Packet.__init__(self, self.type, 0, 0, b'')
        self.key = key
        self.value = value

    def serialize(self) -> bytes:
        return struct.pack(f"b{len(self.key)}sb{len(self.value)}s", len(self.key), self.key.encode(), len(self.value), self.value.encode())

class ListConfigKeys(Packet):
    type: int = 0x53

    def __init__(self):
        Packet.__init__(self, self.type, 0, 0, b'')

class ConfigKey(Packet):
    key: str
    value: str

    def deserialize(self):
        key_len = self.read_values("b")[0]
        self.key = self.read_values(f"{key_len}s")[0].decode()
        value_len = self.read_values("b")[0]
        self.value = self.read_values(f"{value_len}s")[0].decode()

class ClickEventExpired(Packet): ...

class Stop(Packet):
    type: int = 0x56

    def __init__(self):
        Packet.__init__(self, self.type, 0, 0, b'')

class PrototypeWithdraw(Packet): ...

class PluginRestoreWindows(Packet):
    type: int = 0x2F

    def __init__(self):
        Packet.__init__(self, self.type, 0, 0, b'')
