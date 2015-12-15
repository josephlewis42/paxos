''' Copyright 2014 Joseph Lewis III <joseph@josephlewis.net>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

'''

import xml.etree.ElementTree as ET
import sys


messages = []
state_enumerator = 1  # start at 1 because 0 is for the initialization state.
header = None

variables = {}
forward_declares = [] # a set of forward declarations
reset_procedures = [] # a set of code pieces that reset the system


class OutputWriter:
    def __init__(self, indent=""):
        self.string = ""
        self.indent = indent

    def increase_indent(self):
        self.indent += "\t"

    def decrease_indent(self):
        self.indent = self.indent[:-1]

    def add(self, string):
        self.string += self.indent + string + "\n"

    def open_scope(self):
        self.add("{")
        self.increase_indent()

    def close_scope(self):
        self.decrease_indent()
        self.add("}")

    def __str__(self):
        return self.string


HOOKS = {
    "read_front":OutputWriter("\t"),
    "read_front_for":OutputWriter("\t\t")
}


def kill_parser(message):
    print("FATAL ERROR: " + message)
    exit(1)

class StateItem:
    def __init__(self):
        global state_enumerator
        self.state_id = state_enumerator
        state_enumerator += 1
        print "Generating " + str(self) + " with id : " + str(self.state_id)
        self.children = []

class NonChildStateItem(StateItem):
    def __init__(self, node, parent):
        StateItem.__init__(self)
        self.node = node
        self.parent = parent

        for key, value in node.attrib.items():
            setattr(self, key, value)

        if len(node) > 0:
            kill_parser("{} nodes cannot have children".format(self.node.tag))

class PrimaryItem(StateItem):
    def __init__(self, node, name, parent):
        StateItem.__init__(self)
        self.name = name
        self.struct_name = name + "_t"
        self.working_struct = "_" + self.struct_name + "_working"
        self.children = list()
        self.node = node
        self.parent = parent

        for child in node:
            if child.tag == "union":
                self.children.append(Union(child, self))
            elif child.tag == "field":
                self.children.append(Field(child, self))
            elif child.tag == "const":
                self.children.append(Const(child, self))
            elif child.tag == "buffer":
                self.children.append(Buffer(child, self))
            elif child.tag == "checksum_begin":
                self.children.append(ChecksumBegin(child, self))
            elif child.tag == "checksum_end":
                self.children.append(ChecksumEnd(child, self))
            elif child.tag == "flag":
                self.children.append(Flag(child, self))
            else:
                kill_parser("unkonwn item: " + child.tag + " in " + self.name)

    def resolve(self, name):
        for child in self.children:
            for typ, keyword in child.generate_struct():
                if name == keyword:
                    return self.working_struct + "." + keyword
        if self.parent != None:
            return self.parent.resolve(name)
        else:
            kill_parser("Could not resolve: {}".format(name))

class Prefix(PrimaryItem):
    def __init__(self, node):
        PrimaryItem.__init__(self, node, "prefix", None)

    def generate_struct(self):
        items = []
        for child in self.children:
            items += child.generate_struct()
        return items

    def process(self):
        global variables
        variables[self.working_struct] = self.struct_name

        for child in self.children:
            child.process()

        global messages
        for message in messages:
            message.process()

    def get_first_state_id(self):
        ''' Returns the id of the first state that does processing in this generator.
        '''
        global messages

        if len(self.children) > 0:
            return self.children[0].state_id
        elif len(messages) == 1:
            return messages[0].get_first_state_id()
        else:
            kill_parser("Could not choose a first state id, there were no children of header and there were multiple messages to descend to")

    def generate_packer(self, messageName, outputwriter):
        for child in self.children:
            child.generate_packer(messageName, outputwriter)

    def generate_states(self, output):
        output.add("case {}:".format(self.state_id))
        output.open_scope()
        output.add("_state = {};".format(messages[0].get_first_state_id()))
        if len(messages) == 0:
            kill_parser("There must be at least one message defined.")
        elif len(messages) > 1:
            for msg in messages:
                ck = msg.generate_message_check()
                output.add("if({})".format(ck))
                output.add("\t_state = {};".format(msg.get_first_state_id()))
        output.add("break;")
        output.close_scope()

        # generate recursive
        n = len(self.children) - 1
        for i, child in enumerate(self.children):
            next = self.state_id if i == n else self.children[i + 1].state_id
            child.generate_states(output, next)

class Message(PrimaryItem):
    def __init__(self, node):
        global header
        PrimaryItem.__init__(self, node, node.attrib["name"], header)
        self.handler_method_name = "handle_%s" % (self.name)

    def generate_message_check(self):
        checks = []
        keys = self.node.attrib.keys()

        if not "field" in keys:
            kill_parser("field is required in message if there are more than one")
        field = self.node.attrib["field"]

        translations = {"lt":" < ", "gt":" > ", "eq":" == ",
                        "neq":" != ","lte":" <= ","gte":" >= "}

        checks = [c for c in keys if c in translations]
        if len(checks) == 0:
            kill_parser("there are no checks for the message {}".format(self.name))

        full_checks = [self.resolve(field) + translations[c] + self.node.attrib[c] for c in checks]

        return " && ".join(full_checks)

    def generate_struct(self):
        global header
        items = header.generate_struct() # add all common items.
        for child in self.children:
            items += child.generate_struct()
        return items

    def generate_states(self, output):
        global header

        output.add("case {}:".format(self.state_id))
        output.open_scope()
        # copy all headers in to this struct to pack it and be ready to go
        for typ, name in header.generate_struct():
            output.add(self.working_struct + "." + name + " = " + header.resolve(name) + ";")
        output.add(self.handler_method_name + "("+self.working_struct+");")
        output.add("_reset();")
        output.add("break;")
        output.close_scope()

        # generate recursive
        n = len(self.children) - 1
        for i, child in enumerate(self.children):
            next = self.state_id if i == n else self.children[i + 1].state_id
            child.generate_states(output, next)

    def process(self):
        global variables
        variables[self.working_struct] = self.struct_name

        forward_declares.append("void {}({} var); // User supplied".format(self.handler_method_name, self.struct_name))
        for child in self.children:
            child.process()

        global reset_procedures
        reset_procedures.append("%s = (const struct %s){ 0 };" % (self.working_struct, self.struct_name))

    def get_first_state_id(self):
        return self.children[0].state_id if len(self.children) > 0 else self.state_id

    def generate_packer(self):
        global header

        ow = OutputWriter()
        ow.add("void pack_{}({} input, std::vector<char> &message)".format(self.name, self.struct_name))
        ow.open_scope()
        header.generate_packer("message", ow)  # do the header packing first.

        for child in self.children:
            child.generate_packer("message", ow)

        ow.close_scope()
        return str(ow)

class Field(StateItem):
    def __init__(self, node, message):
        StateItem.__init__(self)
        self.message = message
        self.typename = node.attrib['type']
        self.name = node.attrib['name']
        self.working_struct_var = self.message.working_struct + "." + self.name
        self.field_size = "(sizeof("+self.typename+"))"
        self.field_ptr = "((char*) & " + self.working_struct_var + ")"

    def generate_struct(self):
        return [(self.typename, self.name)]

    def generate_code(self, output, next):
        output.add("if (!_read_front({}, {})) break;".format(self.field_size, self.field_ptr))
        output.add("_state = {};".format(next))

    def generate_states(self, output, next):
        output.add("case {}:".format(self.state_id))
        output.open_scope()
        self.generate_code(output, next)
        output.add("break;")
        output.close_scope()

    def process(self):
        #global reset_procedures
        #reset_procedures.append("{} = 0;".format(self.working_struct_var))
        pass

    def generate_packer(self, messageName, ow):
        ow.add("_push_back_generic({}, (char*) &input.{}, {});".format(self.field_size, self.name, messageName))

    def get_value(self, structname=None):
        return self.working_struct_var if structname == None else structname + "." + self.name


class Flag(NonChildStateItem):
    ''' Flags set or clear a single bit, the xml properties have the following
    items:

    name - the name of this flag
    type - the type of the item containing this flag
    offset - the offset of this item in bits from the LSB

    '''
    def __init__(self, node, parent):
        NonChildStateItem.__init__(self, node, parent)
        self.working_struct_var = self.parent.working_struct + "." + self.name
        self.field_size = "(sizeof("+self.type+"))"
        self.field_ptr = "((char*) & " + self.working_struct_var + ")"
        self.offset = eval(self.offset)

    def generate_struct(self):
        return [(self.type, self.name)]

    def generate_code(self, output, next):
        output.add("if (!_read_front({}, {})) break;".format(self.field_size, self.field_ptr))
        output.add("{} = ({} >> {}) & 1;".format(self.working_struct_var, self.working_struct_var, self.offset))
        output.add("_state = {};".format(next))

    def generate_states(self, output, next):
        output.add("case {}:".format(self.state_id))
        output.open_scope()
        self.generate_code(output, next)
        output.add("break;")
        output.close_scope()

    def process(self):
        global reset_procedures
        global HOOKS
        reset_procedures.append("_checksum.clear();")
        reset_procedures.append("_checksum_running = false;")
        variables["_checksum"] = "std::vector<char>"
        variables["_checksum_running"] = "bool"

    def generate_packer(self, messageName, ow):
        ow.add("{} tmp = ({} & 1) << {};".format(self.type, self.working_struct_var, self.offset))
        ow.add("_push_back_generic({}, (char*) &tmp, {});".format(self.read_amt_variable, messageName))

    def get_value(self, structname=None):
        return self.working_struct_var if structname == None else structname + "." + self.name



class ChecksumBegin(StateItem):
    def __init__(self, node, message):
        StateItem.__init__(self)
        self.message = message

    def generate_struct(self):
        return []

    def generate_code(self, output, next):
        output.add("_checksum.clear();");
        output.add("_checksum_running = true;");
        output.add("_state = {};".format(next))

    def generate_states(self, output, next):
        output.add("case {}:".format(self.state_id))
        output.open_scope()
        self.generate_code(output, next)
        output.add("break;")
        output.close_scope()

    def process(self):
        global reset_procedures
        global variables
        global HOOKS
        reset_procedures.append("_checksum.clear();")
        reset_procedures.append("_checksum_running = false;")
        variables["_checksum"] = "std::vector<char>"
        variables["_checksum_running"] = "bool"

        rf = HOOKS["read_front_for"]
        rf.add("//checksum begin")
        rf.add("if(_push_front_amt <= 0 && _checksum_running)")
        rf.open_scope()
        rf.add("_checksum.push_back(_buffer.front());")
        rf.close_scope()
        rf.add("//END checksum begin")



    def generate_packer(self, messageName, ow):
        pass

    def get_value(self, structname=None):
        return None

class ChecksumEnd(StateItem):
    def Fletcher16(self, output, next):
        output.open_scope()
        output.add("uint16_t actual=0, sum1 = 0, sum2 = 0, res;")
        output.add("_checksum_running = false;")
        output.add("if (!_read_front(sizeof(uint16_t), (char*) &actual)) break;")
        output.add("for(char &c : _checksum)")
        output.open_scope()
        output.add("sum1 = (sum1 + (uint8_t) c ) % 256;")
        output.add("sum2 = (sum2 + sum1) % 256;")
        output.close_scope()
        output.add("res =  (sum2 << 8) | sum1;")
        if "debug" in dir(self):
            output.add("printf(\"expected %x got %x\", actual, res);")
        output.add("if(res != actual)")
        output.open_scope()
        output.add(" _die(\"Checksum mismatch\");")
        output.add("break;")
        output.close_scope()
        output.add("_checksum.clear();");
        output.add("_state = {};".format(next))
        output.close_scope()

    def __init__(self, node, message):
        StateItem.__init__(self)
        CHECKSUMS = {"Fletcher16":self.Fletcher16}

        self.message = message
        for key, value in node.attrib.items():
            setattr(self, key, value)
            # should get type
        try:
            self.generate_code = CHECKSUMS[self.type]
        except KeyError:
            kill_parser("{} is an invalid checksum type; choose from: {}".format(self.type, ", ".join(CHECKSUMS.keys())))

    def generate_struct(self):
        return []

    def generate_states(self, output, next):
        output.add("case {}:".format(self.state_id))
        output.open_scope()
        self.generate_code(output, next)
        output.add("break;")
        output.close_scope()

    def process(self):
        global reset_procedures
        global variables
        reset_procedures.append("_checksum.clear();")
        variables["_checksum"] = "std::vector<char>"

    def generate_packer(self, messageName, ow):
        pass

    def get_value(self, structname=None):
        return None

class Buffer(StateItem):
    def __init__(self, node, message):
        StateItem.__init__(self)
        self.message = message

        for key, value in node.attrib.items():
            setattr(self, key, value)
            # should get type, name, length, maxlength

        self.base_type = self.type;
        self.type = "{}[{}]".format(self.type, self.maxlength)
        self.working_struct_var = self.message.working_struct + "." + self.name
        self.field_size = "(sizeof({}))".format(self.type)
        self.field_ptr = "((char*) & " + self.working_struct_var + ")"

        try:
            self.read_amt_variable = eval(self.length)
        except:
            self.read_amt_variable = self.message.working_struct + "." + self.length

    def generate_struct(self):
        return [(self.base_type, "{}[{}]".format(self.name, self.maxlength))]

    def generate_code(self, output, next):
        output.add("if (!_read_front({}, {})) break;".format(self.read_amt_variable, self.field_ptr))
        output.add("_state = {};".format(next))

    def generate_states(self, output, next):
        output.add("case {}:".format(self.state_id))
        output.open_scope()
        self.generate_code(output, next)
        output.add("break;")
        output.close_scope()

    def process(self):
        #global reset_procedures
        #reset_procedures.append("for(int i = 0; i < {}; i++) {}[i] = 0;".format(self.maxlength, self.working_struct_var))
        pass

    def generate_packer(self, messageName, ow):
        ow.add("_push_back_generic(({} * sizeof({}) ), ((char*) &input.{}), {});".format(self.read_amt_variable, self.base_type, self.name, messageName))

    def get_value(self, structname=None):
        return self.working_struct_var if structname == None else structname + "." + self.name


class Union(StateItem):
    def __init__(self, node, message):
        StateItem.__init__(self)
        self.message = message
        print node.attrib
        self.children = []
        self.typename = node.attrib['type']
        self.masks = [eval(x) for x in node.attrib["masks"].split(",")]
        self.working_struct = message.working_struct
        self.field_size = "(sizeof("+self.typename+"))"
        for child in node:
            print child.attrib
            if child.attrib["type"] != node.attrib["type"]:
                print("child types must be the same as parent in a union")
                exit(1)
            if child.tag == "field":
                self.children.append(Field(child, self))
            elif child.tag == "const":
                self.children.append(Const(child, self))
            elif child.tag == "flag":
                self.children.append(Flag(child, self))
            else:
                kill_parser(child.tag + "s are not allowed inside unions")
        if len(self.children) != len(self.masks):
            kill_parser("union must have same number of masks as items.")


    def generate_struct(self):
        items = []
        for child in self.children:
            items += child.generate_struct()
        return items

    def generate_states(self, ow, next):
        ow.add("case {}:".format(self.state_id))
        ow.open_scope()
        ow.add(self.typename + " tmp;")
        ow.add("if (!_read_front(sizeof(%s), (char*) &tmp)) break;" %(self.typename, ))

        for mask in reversed(self.masks):
            ow.open_scope()
            ow.add("%s tmp_inner = tmp & %s;" % (self.typename, mask))
            ow.add("_push_front(sizeof(%s), (char*) & tmp_inner);" % (self.typename,))
            ow.close_scope()

        for child in self.children:
            ow.open_scope()
            child.generate_code(ow, next)
            ow.close_scope()

        ow.add("_state = {};".format(next))
        ow.add("break;")
        ow.close_scope()

    def process(self):
        for child in self.children:
            child.process()

    def generate_packer(self, messageName, ow):
        ow.open_scope()
        ow.add("{} temp = {};".format(self.typename, self.get_value("input")))
        ow.add("_push_back_generic({}, (char*) &temp, {});".format(self.field_size, messageName))
        ow.close_scope()

    def get_value(self, structname=None):
        internal = []
        for num, child in enumerate(self.children):
            internal.append("({} & {})".format(self.masks[num], child.get_value(structname)))

        return "(" + " + ".join(internal) + ")"

    def resolve(self, name):
        for child in self.children:
            for typ, keyword in child.generate_struct():
                if name == keyword:
                    return self.working_struct + "." + keyword
        if self.message != None:
            return parent.resolve()
        else:
            kill_parser("Could not resolve: {}".format(name))

class Const(StateItem):
    def __init__(self, node, message):
        self.message = message
        self.value = eval(node.attrib['value'])
        self.typename = node.attrib['type']
        self.field_size = "(sizeof("+self.typename+"))"
        StateItem.__init__(self)

    def generate_code(self, output, after):
        output.add(self.typename + " tmp;")
        output.add("if (!_read_front(sizeof(%s), (char*) &tmp)) break;" %(self.typename, ))
        output.add("if (tmp != %s)" %(self.value))
        output.open_scope()
        output.add("_die(\"Constant not matched.\");")
        output.add("break;")
        output.close_scope()
        output.add("_state = {};".format(after))

    def generate_states(self, output, next):
        output.add("case {}:".format(self.state_id))
        output.open_scope()
        self.generate_code(output, next)
        output.add("break;")
        output.close_scope()

    def generate_struct(self):
        return []

    def process(self):
        pass

    def generate_packer(self, messageName, ow):
        ow.open_scope()
        ow.add("{} tmp = {};".format(self.typename, self.value))
        ow.add("_push_back_generic({}, (char*) &tmp, {});".format(self.field_size, messageName))
        ow.close_scope()

    def get_value(self, structname=None):
        return self.value


GENERATOR_PROPERTIES = {}


c = '''
#ifndef {namespace}_PARSER_HPP
#define {namespace}_PARSER_HPP

#include <deque>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {namespace}
{{

// User supplied typdefs
{typedefs}

// Message definitions
{structs}

// Forward declarations
{forwards}
void handle_invalid_message(const char* message); // usesupplied, when the parser encounters an error

std::deque<char> _buffer;

int _state = 0;
{variables}

void _reset()
{{
    _state = 0;
    {reset_code}
}}

void _die(const char* message)
{{
    _reset();
    handle_invalid_message(message);
}}

void _push_back(int length, char* buffer)
{{
    for(int i = 0; i < length; i++)
    {{
        _buffer.push_back(buffer[i]);
    }}
}}


void _push_back_generic(int length, char* buffer, std::vector<char> &outputbuf)
{{
    for(int i = 0; i < length; i++)
    {{
        outputbuf.push_back(buffer[i]);
    }}
}}

int _push_front_amt = 0;
void _push_front(int length, char* buffer)
{{
    _push_front_amt += length;
    for(int i = length - 1; i >= 0; i--)
    {{
        _buffer.push_front(buffer[i]);
    }}
}}

bool _read_front(int length, char* outbuffer)
{{
    if(_buffer.size() < length)
        return false;

    {read_front}

    for(int i = 0; i < length; i++)
    {{
        {read_front_for}
        outbuffer[i] = _buffer.front();
        _buffer.pop_front();
    }}

    if(_push_front_amt > 0)
        _push_front_amt -= length;

    return true;
}}

void _process()
{{
    // do cleanup of a possible remaining useless variables.
    char a;
    for(int i = 0; i < _push_front_amt; i++)
        _read_front(1, &a);

    //printf("Current state: %d\\n", _state);
    switch(_state)
    {{
        case 0: // initial state
            _reset(); // reset all variables
            _state = {initial_state};
            break;
        {switches}

        default:
            _die("could not parse the given message, invalid state reached");
            _state = 0; // reset to initial state
    }}
}}

void update(int length, char* buffer)
{{
    _push_back(length, buffer);

    // Keep processing until done.
    int laststate = _state;
    do
    {{
        laststate = _state;
        _process();
    }} while(laststate != _state);
}}

void clear()
{{
    _push_front_amt = 0;
    _buffer.clear();
    _reset();
}}

{packs}

}} // end namespace
#endif //{namespace}_PARSER_HPP
'''


def generate_structs():
    out = ""


    for msg in messages + [header]:
        out += """
struct %s {
\t%s
};\n""" % (msg.struct_name, "\n\t".join([" ".join(i) + ";" for i in msg.generate_struct()]))
    return out

def generate_switches():
    out = OutputWriter()

    for msg in messages + [header]:
        msg.generate_states(out)

    return str(out)

def parse(input_file, output_file):
    global header
    global messages

    tree = ET.parse(input_file)

    root = tree.getroot()

    if root.attrib['version'] != "1.0":
        kill_parser("version is invalid for this generator")


    for child in root:
        if child.tag == "message":
            messages.append(Message(child))
        elif child.tag == "header":
            header = Prefix(child)
        elif child.tag == "generation":
            for prop in child:
                GENERATOR_PROPERTIES[prop.tag] = prop.text
        else:
            kill_parser("Unknown node type: " + child.tag)


    print("Parsing Completed")

    print("\tfound %s messages" % (len(messages)))
    print("\tfound %s properties" % (len(GENERATOR_PROPERTIES)))

    header.process()

    d = dict({
    "variables" :  "\n".join("%s %s;" % (t, n) for n, t in variables.items()),
    "reset_code" : "\n".join(reset_procedures),
    "structs" : generate_structs(),
    "switches" : generate_switches(),
    "forwards" : "\n\t".join(forward_declares),
    "initial_state" : header.get_first_state_id(),
    "packs" : "\n".join([m.generate_packer() for m in messages]),
    "typedefs":""
    }.items() + GENERATOR_PROPERTIES.items() + HOOKS.items())

    with open(output_file, 'w') as out:
        out.write(c.format(**d))

    print generate_switches()

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print "Usage: {} input output".format(sys.argv[0])
        exit(1)
    parse(sys.argv[1], sys.argv[2])
