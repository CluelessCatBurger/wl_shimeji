import xml
from xml.etree import ElementTree
import json
import enum

INLINED_ACTION_AUTOINCREMENT = 0
INLINED_BEHAVIOUR_AUTOINCREMENT = 0
INLINED_CONDITION_AUTOINCREMENT = 0

class RedefinitionError(Exception):
    def __init__(self, message):
        super().__init__(message)

ACTION_TYPE = enum.Enum("ActionType", [
    "Fall",
    "Move",
    "Embedded",
    "Stay",
    "Animate",
    "Sequence",
    "Select"
])

EMBEDDED_TYPE = enum.Enum("EmbeddedType", [
    "Jump",
    "Fall",
    "Look",
    "Offset",
    "FallWithIE",
    "JumpWithIE",
    "WalkWithIE",
    "ThrowIE",
    "Dragged",
    "Resist",
    "Breed",
    "Broadcast",
    "ScanMove",
    "Interact",
    "Transform",
    "Scanjump",
    "Dispose",
    "Mute"
])

ACTION_CONTENT_TYPE = enum.Enum("ActionContentType", [
    "ActionReference",
    "Animation",
])

CLASS_NAME_TO_EMBEDDED_TYPE = {
    f"com.group_finity.mascot.action.{embedded_type.name}": embedded_type
    for embedded_type in EMBEDDED_TYPE
}

CLASS_NAME_TO_EMBEDDED_TYPE["com.group_finity.mascot.action.Regist"] = EMBEDDED_TYPE.Resist
CLASS_NAME_TO_EMBEDDED_TYPE["com.group_finity.mascot.action.SelfDestruct"] = EMBEDDED_TYPE.Dispose

namespace = {'ns': 'http://www.group-finity.com/Mascot'}

MASCOT_VAR_NAMES = [
            "X",
            "Y",
            "TargetX",
            "TargetY",
            "VelocityParam",
            "InitialVX",
            "InitialVY",
            "Gravity",
            "RegistanceX",
            "RegistanceY",
            "LookRight",
            "IeOffsetX",
            "IeOffsetY",
            "BornX",
            "BornY",
            "Duration",
            "BornInterval",
            "BornCount",
            "BornTransient",
            # "BornBehaviour",
            # "BornMascot",
            # "TransformBehaviour",
            # "TransformMascot",
            # "Affordance",
            # "Behaviour",
            # "TargetBehaviour",
            "Loop",
            "Condition",
            # "Velocity",
            "Duration",
            "FootX",
            "FootDX",
            "OffsetX",
            "OffsetY",
            "Gap"
]

def vector_to_tuple(what: str) -> tuple[int, int]:
    if what is None:
        return (0, 0)
    vars = what.split(",")
    if len(vars) != 2:
        return (int(vars[0]), int(vars[0]))
    return (int(vars[0]), int(vars[1]))

def parse_animation(animation: ElementTree.Element, programs_defs: list[str]) -> dict:
    if animation.tag != "{http://www.group-finity.com/Mascot}Animation":
        raise SyntaxError("Invalid XML format: Expected Animation")

    animation_obj = {
        "type": "Animation",
        "condition": programs_defs.index(animation.attrib.get("Condition")) if animation.attrib.get("Condition") else None,
        "frames": [],
        "hotspots": [],
        "frame_count": 0,
        "hotspots_count": 0
    }

    for child in animation:
        if child.tag == "{http://www.group-finity.com/Mascot}Pose":
            frame_obj = {
                "type": "Frame",
                "image": child.attrib.get("Image"),
                "image_right": child.attrib.get("ImageRight"),
                "image_anchor_x": vector_to_tuple(child.attrib.get("ImageAnchor"))[0],
                "image_anchor_y": vector_to_tuple(child.attrib.get("ImageAnchor"))[1],
                "velocity_x": vector_to_tuple(child.attrib.get("Velocity"))[0],
                "velocity_y": vector_to_tuple(child.attrib.get("Velocity"))[1],
                "duration": int(child.attrib.get("Duration", 0))
            }

            if frame_obj["image"]:
                frame_obj["image"] = frame_obj["image"].lstrip("/").replace('.png', '.qoi')
            if frame_obj["image_right"]:
                frame_obj["image_right"] = frame_obj["image_right"].lstrip("/").replace('.png', '.qoi')

            animation_obj["frames"].append(frame_obj)
            animation_obj["frame_count"] += 1
        elif child.tag == "{http://www.group-finity.com/Mascot}Hotspot":
            hotspot_obj = {
                "type": "Hotspot",
                "shape": child.get("Shape", "Rectangle"),
                "x": int(child.get("Origin", "0,0").split(",")[0]),
                "y": int(child.get("Origin", "0,0").split(",")[1]),
                "width": int(child.get("Size", "0,0").split(",")[0]),
                "height": int(child.get("Size", "0,0").split(",")[1]),
                "behavior": child.get("Behavior", None)
            }

            animation_obj["hotspots"].append(hotspot_obj)
            animation_obj["hotspots_count"] += 1
        else:
            print(f"Unknown tag in animation: {child.tag}; skipping...")
            continue

    return animation_obj



def parse_action_reference(actionref: ElementTree.Element, action_definitions: dict, programs_defs: list[str]) -> dict:
    if actionref.tag != "{http://www.group-finity.com/Mascot}ActionReference":
        raise SyntaxError("Invalid XML format: Expected ActionReference")

    action_name = actionref.attrib.get("Name")
    if action_name is None:
        raise SyntaxError("Invalid XML format: ActionReference name not found")

    actionref_obj = {
        "type": "ActionReference",
        "action_name": action_name,
        "duration": programs_defs.index(actionref.attrib.get("Duration")) if actionref.attrib.get("Duration") else None,
        "condition": programs_defs.index(actionref.attrib.get("Condition")) if actionref.attrib.get("Condition") else None,
        "locals_overrides": {},
        "locals_count": 0,
    }

    for attrib_name, attrib_value in actionref.attrib.items():
        if attrib_name in MASCOT_VAR_NAMES:
            actionref_obj["locals_overrides"][f"mascot.{attrib_name}"] = programs_defs.index(attrib_value)
            actionref_obj["locals_count"] += 1

    if actionref_obj["locals_overrides"].pop("mascot.Duration", None):
        actionref_obj["locals_count"] -= 1

    if actionref_obj["locals_overrides"].pop("mascot.Loop", None):
        actionref_obj["locals_count"] -= 1

    return actionref_obj

def parse_action(action: ElementTree.Element, action_definitions: dict, programs_defs = list[str], depth: int = 0) -> dict:
    global INLINED_ACTION_AUTOINCREMENT

    action_type = action.attrib.get("Type")
    if action_type is None:
        raise SyntaxError("Invalid XML format: Action type not found")

    action_type = ACTION_TYPE[action_type]

    action_object = {
        "type": action_type.name,
        "name": action.attrib.get("Name", None),
        "content": [],
        "content_count": 0,
        "local_variables": {},
        "local_variables_count": 0,
        "embedded_type": CLASS_NAME_TO_EMBEDDED_TYPE.get(action.attrib.get("Class")).name if action.attrib.get("Class") else None,
        "loop": {"true": True, "false": False}.get(action.attrib.get("Loop", False), False),
        "condition": programs_defs.index(action.attrib.get("Condition")) if action.attrib.get("Condition") else None,
        "border_type": action.attrib.get("BorderType", "Any"),
    }

    if action_object["name"] == None:
        if depth == 0:
            raise SyntaxError("Invalid XML format: Root action must have a name")
        action_object["name"] = f"___INLINED_ACTION_{INLINED_ACTION_AUTOINCREMENT}"
        INLINED_ACTION_AUTOINCREMENT += 1

    for child in action:
        if child.tag == "{http://www.group-finity.com/Mascot}Action":
            if action_type not in [ACTION_TYPE.Sequence, ACTION_TYPE.Select]:
                print(f"{' ' * depth}Warning: Action type {action_type} does not support nested actions")
                continue
            action_obj = parse_action(child, action_definitions, programs_defs, depth + 1)
            action_object["content"].append(
                {
                    "type": "ActionReference",
                    "action_name": action_obj["name"],
                    "duration": programs_defs.index(child.attrib.get("Duration")) if child.attrib.get("Duration") else None,
                    "condition": programs_defs.index(child.attrib.get("Condition")) if child.attrib.get("Condition") else None,
                    "locals_overrides": {},
                    "locals_count": 0,
                }
            )
            action_object["content_count"] += 1
        elif child.tag == "{http://www.group-finity.com/Mascot}ActionReference":
            if action_type not in [ACTION_TYPE.Sequence, ACTION_TYPE.Select]:
                print(f"{' ' * depth}Warning: Action type {action_type} does not support nested actions")
                continue
            actionref = parse_action_reference(child, action_definitions, programs_defs)
            action_object["content"].append(actionref)
            action_object["content_count"] += 1

        elif child.tag == "{http://www.group-finity.com/Mascot}Animation":
            if action_type in [ACTION_TYPE.Sequence, ACTION_TYPE.Select]:
                print(f"{' ' * depth}Warning: Action type {action_type} does not support animations")
                continue
            animation_obj = parse_animation(child, programs_defs)
            action_object["content"].append(animation_obj)
            action_object["content_count"] += 1

    for attrib_name, attrib_value in action.attrib.items():
        if attrib_name in MASCOT_VAR_NAMES:
            action_object["local_variables"][f"mascot.{attrib_name}"] = programs_defs.index(attrib_value)
            action_object["local_variables_count"] += 1
        if attrib_name == "TargetBehavior":
            action_object["target_behavior"] = attrib_value
        if attrib_name == "BornBehavior":
            action_object["born_behavior"] = attrib_value
        if attrib_name == "SelectBehavior":
            action_object["select_behavior"] = attrib_value
        if attrib_name == "Affordance":
            action_object["affordance"] = attrib_value
        if attrib_name == "TransformMascot":
            action_object["transform_target"] = attrib_value
        if attrib_name == "Behavior":
            action_object["behavior"] = attrib_value
        if attrib_name == "BornMascot":
            action_object["born_mascot"] = attrib_value
        if attrib_name == "TargetLook":
            action_object["target_look"] = {"true": True, "false": False}.get(attrib_value, False)

    if action_object["name"] in action_definitions:
        raise SyntaxError(f"Action {action_object['name']} redefinition")

    if action_object["local_variables"].pop("mascot.Duration", None):
        action_object["local_variables_count"] -= 1

    if action_object["local_variables"].pop("mascot.Loop", None):
        action_object["local_variables_count"] -= 1

    action_definitions[action_object["name"]] = action_object
    return action_object


def parse_action_list(actions_list: ElementTree.Element, action_definitions: dict, programs_defs: list[str]):
    for action in actions_list:
        if action.tag == "{http://www.group-finity.com/Mascot}Action":
            action_object = parse_action(action, action_definitions, programs_defs)
        else:
            raise SyntaxError("Invalid XML format")

def behavior_to_ref(behavior: dict) -> dict:
    behavior_reference = {
        "name": behavior["name"],
        "frequency": behavior["frequency"],
    }

    return behavior_reference

def parse_behavior(behavior: ElementTree.Element, behavior_definitions: dict, action_definitions: dict, programs_defs: list[str]) -> dict:
    global INLINED_BEHAVIOUR_AUTOINCREMENT

    behavior_object = {
        "name": behavior.attrib.get("Name", None),
        "action": None,
        "next_behavior_list": [],
        "next_behavior_list_count": 0,
        "hidden": False,
        "condition": programs_defs.index(behavior.attrib.get("Condition")) if behavior.attrib.get("Condition") else None,
        "is_conditioner": False,
        "next_behavior_list_add": True,
        "frequency": int(behavior.attrib.get("Frequency", 0))
    }

    if behavior.tag == "{http://www.group-finity.com/Mascot}Condition":
        behavior_object["name"] = f"___CONDITION_{INLINED_BEHAVIOUR_AUTOINCREMENT}"
        INLINED_BEHAVIOUR_AUTOINCREMENT += 1
        behavior_object["is_conditioner"] = True
        behavior_object["hidden"] = True
        behavior_object["frequency"] = 0

        for child in behavior:
            try:
                new_behavior = parse_behavior(child, behavior_definitions, action_definitions, programs_defs)
                behavior_object["next_behavior_list"].append(behavior_to_ref(new_behavior))
                behavior_object["next_behavior_list_count"] += 1
            except RedefinitionError:
                behavior_reference = {
                    "name": child.attrib.get("Name"),
                    "frequency": int(child.attrib.get("Frequency", 0)),
                }
                behavior_object["next_behavior_list"].append(behavior_reference)
                behavior_object["next_behavior_list_count"] += 1

    elif behavior.tag == "{http://www.group-finity.com/Mascot}Behavior":
        for child in behavior:
            if child.tag == "{http://www.group-finity.com/Mascot}NextBehaviorList":
                if child.attrib.get("Add", "true") == "false":
                    behavior_object["next_behavior_list_add"] = False
                for subchild in child:
                    if subchild.tag == "{http://www.group-finity.com/Mascot}Behavior":
                        try:
                            new_behavior = parse_behavior(subchild, behavior_definitions, action_definitions, programs_defs)
                            behavior_object["next_behavior_list"].append(behavior_to_ref(new_behavior))
                            behavior_object["next_behavior_list_count"] += 1
                        except RedefinitionError:
                            behavior_reference = {
                                "name": subchild.attrib.get("Name"),
                                "frequency": int(subchild.attrib.get("Frequency", 0)),
                            }
                            behavior_object["next_behavior_list"].append(behavior_reference)
                            behavior_object["next_behavior_list_count"] += 1
                    elif subchild.tag == "{http://www.group-finity.com/Mascot}Condition":
                        new_behavior = parse_behavior(subchild, behavior_definitions, action_definitions, programs_defs)
                        behavior_object["next_behavior_list"].append(behavior_to_ref(new_behavior))
                        behavior_object["next_behavior_list_count"] += 1
                    elif subchild.tag == "{http://www.group-finity.com/Mascot}BehaviorReference":
                        behavior_reference = {
                            "name": subchild.attrib.get("Name"),
                            "frequency": int(subchild.attrib.get("Frequency", 0)),
                        }
                        behavior_object["next_behavior_list"].append(behavior_reference)
                        behavior_object["next_behavior_list_count"] += 1
                    else:
                        raise SyntaxError("Invalid XML format")
        if behavior.get("Action") is not None:
            behavior_object["action"] = behavior.get("Action")
        else:
            behavior_object["action"] = behavior_object["name"]
        if behavior_object["action"] not in action_definitions:
            raise SyntaxError(f"Action {behavior_object['action']} not defined")
    else:
        raise SyntaxError("Invalid XML format")

    if behavior_object["name"] in behavior_definitions:
        raise RedefinitionError(f"Behavior {behavior_object['name']} redefinition")
    behavior_definitions[behavior_object["name"]] = behavior_object
    return behavior_object



def parse_behavior_list(behavior: ElementTree.Element, behavior_definitions: dict, action_definitions: dict, programs_defs: list[str], root_behavior_list) -> dict:
    for child in behavior:
        if child.tag == "{http://www.group-finity.com/Mascot}Behavior" or child.tag == "{http://www.group-finity.com/Mascot}Condition":
            behavior_object = parse_behavior(child, behavior_definitions, action_definitions, programs_defs)
            root_behavior_list.append(behavior_to_ref(behavior_object))
        else:
            raise SyntaxError("Invalid XML format")


def shmconv(actions: str, behaviors: str) -> tuple[list[str], dict, tuple[dict,list]]:

    """
    This functions shimeji-ee defintions to json format
    :param actions: str: actions in shimeji-ee format (xml)
    :param behaviors: str: behaviors in shimeji-ee format (xml)
    """

    # Read the xml strings provided as args
    try:
        actions_parsed = ElementTree.fromstring(actions)
        behaviors_parsed = ElementTree.fromstring(behaviors)

        acts = {}
        behs = {}

        # First find all attrubutes recursively
        # All attributes that starts with ${ or #{ considered as a candidate for program
        # All attributes that does not start with ${ or #{ but set some sort of value to mascot's variable considered as a candidate for program

        # Assuming actions_parsed and behaviors_parsed are already defined XML trees
        program_candidates = []

        # Iterate over actions
        for element in actions_parsed.iter():
            for attr_name, attr_content in element.attrib.items():
                if attr_content in program_candidates:
                    continue
                if attr_name in MASCOT_VAR_NAMES:
                    program_candidates.append(attr_content)
                elif attr_content.startswith("${") or attr_content.startswith("#{"):
                    program_candidates.append(attr_content)

        # Iterate over behaviors
        for element in behaviors_parsed.iter():
            for attr_name, attr_content in element.attrib.items():
                if attr_content in program_candidates:
                    continue
                if attr_name in MASCOT_VAR_NAMES:
                    program_candidates.append(attr_content)
                elif attr_content.startswith("${") or attr_content.startswith("#{"):
                    program_candidates.append(attr_content)

        # Now parse the actions
        action_definitions = {}
        for action_list in actions_parsed.findall("ns:ActionList", namespaces=namespace):
            parse_action_list(action_list, action_definitions, program_candidates)

        # Sort action by type: definitions should be always first
        action_definitions = dict(sorted(action_definitions.items(), key=lambda item: item[1]["type"] in ["Sequence", "Select"]))

        # Parse behaviors
        behavior_definitions = {}
        root_behavior_list = []
        for behavior_list in behaviors_parsed.findall("ns:BehaviorList", namespaces=namespace):
            parse_behavior_list(behavior_list, behavior_definitions, action_definitions, program_candidates, root_behavior_list)

        # Sanity checks:
        if "Fall" not in behavior_definitions:
            print("Warning: Fall behavior not defined; It is required for execution and will not load")
        if "Dragged" not in behavior_definitions:
            print("Warning: Dragged behavior not defined; It is required for execution and will not load")
        if "Thrown" not in behavior_definitions:
            print("Warning: Thrown behavior not defined; It is required for execution and will not load")


        return program_candidates, action_definitions, (behavior_definitions, root_behavior_list)
    except Exception as e:
           raise e
           raise SyntaxError("Invalid XML format")
    # raise NotImplementedError("Not implemented yet")
