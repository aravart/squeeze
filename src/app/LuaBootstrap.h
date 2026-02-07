#pragma once

namespace squeeze {

inline constexpr const char* luaBootstrap = R"lua(
-- PluginNode metatable
local PluginNode = {}
PluginNode.__index = PluginNode

function PluginNode:open_editor()
    return sq.open_editor(self.id)
end

function PluginNode:close_editor()
    return sq.close_editor(self.id)
end

function PluginNode:set_param(name, value)
    return sq.set_param(self.id, name, value)
end

function PluginNode:get_param(name)
    return sq.get_param(self.id, name)
end

function PluginNode:params()
    return sq.params(self.id)
end

function PluginNode:param_info()
    return sq.param_info(self.id)
end

function PluginNode:param_text(name_or_index)
    return sq.param_text(self.id, name_or_index)
end

function PluginNode:set_param_i(index, value)
    return sq.set_param_i(self.id, index, value)
end

function PluginNode:get_param_i(index)
    return sq.get_param_i(self.id, index)
end

function PluginNode:ports()
    return sq.ports(self.id)
end

function PluginNode:remove()
    return sq.remove_node(self.id)
end

function PluginNode:__tostring()
    return "PluginNode(" .. self.id .. ", \"" .. self.name .. "\")"
end

-- MidiInputNode metatable
local MidiInputNode = {}
MidiInputNode.__index = MidiInputNode

function MidiInputNode:remove()
    return sq.remove_node(self.id)
end

function MidiInputNode:__tostring()
    return "MidiInputNode(" .. self.id .. ", \"" .. self.name .. "\")"
end

-- Wrap sq.add_plugin to return a PluginNode object
local raw_add_plugin = sq.add_plugin
sq.add_plugin = function(name)
    local id, err = raw_add_plugin(name)
    if not id then return nil, err end
    local node = { id = id, name = name }
    setmetatable(node, PluginNode)
    return node
end

-- Wrap sq.add_midi_input to return a MidiInputNode object
local raw_add_midi_input = sq.add_midi_input
sq.add_midi_input = function(name)
    local id, err = raw_add_midi_input(name)
    if not id then return nil, err end
    local node = { id = id, name = name }
    setmetatable(node, MidiInputNode)
    return node
end

-- Wrap sq.connect to accept node objects or bare IDs
local raw_connect = sq.connect
sq.connect = function(src, src_port, dst, dst_port, midi_channel)
    local src_id = type(src) == "table" and src.id or src
    local dst_id = type(dst) == "table" and dst.id or dst
    return raw_connect(src_id, src_port, dst_id, dst_port, midi_channel)
end
)lua";

} // namespace squeeze
