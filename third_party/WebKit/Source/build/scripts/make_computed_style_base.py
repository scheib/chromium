#!/usr/bin/env python
# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import math
import sys

import json5_generator
import template_expander
import make_style_builder

from name_utilities import (
    enum_for_css_keyword, enum_type_name, enum_value_name, class_member_name, method_name,
    class_name, join_name
)
from collections import defaultdict, OrderedDict
from itertools import chain

# Heuristic ordering of types from largest to smallest, used to sort fields by their alignment sizes.
# Specifying the exact alignment sizes for each type is impossible because it's platform specific,
# so we define an ordering instead.
# The ordering comes from the data obtained in:
# https://codereview.chromium.org/2841413002
# TODO(shend): Put alignment sizes into code form, rather than linking to a CL which may disappear.
ALIGNMENT_ORDER = [
    'double',
    'Font', 'FillLayer', 'NinePieceImage',  # Aligns like a void * (can be 32 or 64 bits)
    'LengthBox', 'LengthSize', 'Length', 'float',
    'StyleColor', 'Color', 'unsigned', 'int',
    'short',
    'char',
    'bool'
]

# TODO(shend): Improve documentation and add docstrings.


def _flatten_list(x):
    """Flattens a list of lists into a single list."""
    return list(chain.from_iterable(x))


def _num_32_bit_words_for_bit_fields(bit_fields):
    """Gets the number of 32 bit unsigned integers needed store a list of bit fields."""
    num_buckets, cur_bucket = 0, 0
    for field in bit_fields:
        if field.size + cur_bucket > 32:
            num_buckets += 1
            cur_bucket = 0
        cur_bucket += field.size
    return num_buckets + (cur_bucket > 0)


class Group(object):
    """Represents a group of fields stored together in a class.

    Attributes:
        name: The name of the group as a string.
        subgroups: List of Group instances that are stored as subgroups under this group.
        fields: List of Field instances stored directly under this group.
    """
    def __init__(self, name, subgroups, fields):
        self.name = name
        self.subgroups = subgroups
        self.fields = fields
        self.type_name = class_name(join_name('style', name, 'data'))
        self.member_name = class_member_name(join_name(name, 'data'))
        self.num_32_bit_words_for_bit_fields = _num_32_bit_words_for_bit_fields(
            field for field in fields if field.is_bit_field
        )

        # Recursively get all the fields in the subgroups as well
        self.all_fields = _flatten_list(subgroup.all_fields for subgroup in subgroups) + fields


class DiffGroup(object):
    """Represents a group of expressions and subgroups that need to be diffed
    for a function in ComputedStyle.

    Attributes:
        subgroups: List of DiffGroup instances that are stored as subgroups under this group.
        expressions: List of expression that are on this group that need to be diffed.
    """
    def __init__(self, group_name):
        self.group_name = group_name
        self.subgroups = []
        self.expressions = []


class Field(object):
    """
    The generated ComputedStyle object is made up of a series of Fields.
    Each Field has a name, size, type, etc, and a bunch of attributes to
    determine which methods it will be used in.

    A Field also has enough information to use any storage type in C++, such as
    regular member variables, or more complex storage like vectors or hashmaps.
    Almost all properties will have at least one Field, often more than one.

    Most attributes in this class correspond to parameters in CSSProperties.json5.
    See that file for a more detailed explanation of each attribute.

    Attributes:
        field_role: The semantic role of the field. Can be:
            - 'property': for fields that store CSS properties
            - 'inherited_flag': for single-bit flags that store whether a property is
                                inherited by this style or set explicitly
        name_for_methods: String used to form the names of getters and setters.
            Should be in upper camel case.
        property_name: Name of the property that the field is part of.
        type_name: Name of the C++ type exposed by the generated interface (e.g. EClear, int).
        field_template: Determines the interface generated for the field. Can be one of:
           keyword, flag, or monotonic_flag.
        field_group: The name of the group that this field is inside.
        size: Number of bits needed for storage.
        default_value: Default value for this field when it is first initialized.
    """

    def __init__(self, field_role, name_for_methods, property_name, type_name,
                 field_template, field_group, size, default_value, has_custom_compare_and_copy,
                 getter_method_name, setter_method_name, initial_method_name, **kwargs):
        """Creates a new field."""
        self.name = class_member_name(name_for_methods)
        self.property_name = property_name
        self.type_name = type_name
        self.field_template = field_template
        self.group_name = field_group
        self.group_member_name = class_member_name(join_name(field_group, 'data')) if field_group else None
        self.size = size
        self.default_value = default_value
        self.has_custom_compare_and_copy = has_custom_compare_and_copy

        # Field role: one of these must be true
        self.is_property = field_role == 'property'
        self.is_inherited_flag = field_role == 'inherited_flag'
        assert (self.is_property, self.is_inherited_flag).count(True) == 1, \
            'Field role has to be exactly one of: property, inherited_flag'

        if not self.is_inherited_flag:
            self.is_inherited = kwargs.pop('inherited')
            self.is_independent = kwargs.pop('independent')
            assert self.is_inherited or not self.is_independent, 'Only inherited fields can be independent'

            self.is_inherited_method_name = method_name(join_name(name_for_methods, 'is inherited'))

        # Method names
        # TODO(nainar): Method name generation is inconsistent. Fix.
        self.getter_method_name = getter_method_name
        self.setter_method_name = setter_method_name
        self.internal_getter_method_name = method_name(join_name(self.name, 'Internal'))
        self.internal_mutable_method_name = method_name(join_name('Mutable', name_for_methods, 'Internal'))
        self.internal_setter_method_name = method_name(join_name(setter_method_name, 'Internal'))
        self.initial_method_name = initial_method_name
        self.resetter_method_name = method_name(join_name('Reset', name_for_methods))
        if self.group_name:
            self.getter_expression = self.group_member_name + '->' + class_member_name(self.name)
        else:
            self.getter_expression = class_member_name(self.name)

        # If the size of the field is not None, it means it is a bit field
        self.is_bit_field = self.size is not None

        assert len(kwargs) == 0, 'Unexpected arguments provided to Field: ' + str(kwargs)


def _get_include_paths(properties):
    """
    Get a list of paths that need to be included for ComputedStyleBase.
    """
    include_paths = set()
    for property_ in properties:
        if property_['field_type_path'] is not None:
            include_paths.add(property_['field_type_path'] + '.h')
    return list(sorted(include_paths))


def _group_fields(fields):
    """Groups a list of fields by their group_name and returns the root group."""
    groups = defaultdict(list)
    for field in fields:
        groups[field.group_name].append(field)

    no_group = groups.pop(None)
    subgroups = [Group(group_name, [], _reorder_fields(fields)) for group_name, fields in groups.items()]
    return Group('', subgroups=subgroups, fields=_reorder_fields(no_group))


def _create_diff_groups_map(diff_function_inputs, root_group):
    diff_functions_map = {}
    for entry in diff_function_inputs:
        diff_functions_map[entry['name']] = _create_diff_groups(entry['fields_to_diff'], entry['methods_to_diff'], root_group)
    return diff_functions_map


def _list_field_dependencies(methods_to_diff):
    field_dependencies = []
    for entry in methods_to_diff:
        field_dependencies += entry['field_dependencies']
    return field_dependencies


def _create_diff_groups(fields_to_diff, methods_to_diff, root_group):
    diff_group = DiffGroup(root_group.member_name)
    field_dependencies = _list_field_dependencies(methods_to_diff)
    for subgroup in root_group.subgroups:
        if any(field.property_name in (fields_to_diff + field_dependencies) for field in subgroup.all_fields):
            diff_group.subgroups.append(_create_diff_groups(fields_to_diff, methods_to_diff, subgroup))
    for field in root_group.fields:
        if field.property_name in fields_to_diff:
            diff_group.expressions.append(field.getter_expression)
        for entry in methods_to_diff:
            if field.property_name in entry['field_dependencies']:
                diff_group.expressions.append(entry['method'])
    return diff_group


def _create_enums(properties):
    """
    Returns an OrderedDict of enums to be generated, enum name -> [list of enum values]
    """
    enums = {}
    for property_ in properties:
        # Only generate enums for keyword properties that use the default field_type_path.
        if property_['field_template'] == 'keyword' and property_['field_type_path'] is None:
            enum_name = property_['type_name']
            enum_values = [enum_value_name(k) for k in property_['keywords']]

            if enum_name in enums:
                # There's an enum with the same name, check if the enum values are the same
                assert set(enums[enum_name]) == set(enum_values), \
                    ("'" + property_['name'] + "' can't have type_name '" + enum_name + "' "
                     "because it was used by a previous property, but with a different set of keywords. "
                     "Either give it a different name or ensure the keywords are the same.")

            enums[enum_name] = enum_values

    # Return the enums sorted by key (enum name)
    return OrderedDict(sorted(enums.items(), key=lambda t: t[0]))


def _create_property_field(property_):
    """
    Create a property field.
    """
    name_for_methods = property_['name_for_methods']

    assert property_['default_value'] is not None, \
        ('MakeComputedStyleBase requires an default value for all fields, none specified '
         'for property ' + property_['name'])

    if property_['field_template'] == 'keyword':
        type_name = property_['type_name']
        default_value = type_name + '::' + enum_value_name(property_['default_value'])
        size = int(math.ceil(math.log(len(property_['keywords']), 2)))
    elif property_['field_template'] == 'storage_only':
        # 'storage_only' fields need to specify a size, type_name and default_value
        type_name = property_['type_name']
        default_value = property_['default_value']
        size = property_['field_size']
    elif property_['field_template'] == 'external':
        type_name = property_['type_name']
        default_value = property_['default_value']
        size = None
    elif property_['field_template'] == 'primitive':
        type_name = property_['type_name']
        default_value = property_['default_value']
        size = 1 if type_name == 'bool' else None  # pack bools with 1 bit.
    else:
        assert property_['field_template'] in ('monotonic_flag',)
        type_name = 'bool'
        default_value = 'false'
        size = 1

    return Field(
        'property',
        name_for_methods,
        property_name=property_['name'],
        inherited=property_['inherited'],
        independent=property_['independent'],
        type_name=type_name,
        field_template=property_['field_template'],
        field_group=property_['field_group'],
        size=size,
        default_value=default_value,
        has_custom_compare_and_copy=property_['has_custom_compare_and_copy'],
        getter_method_name=property_['getter'],
        setter_method_name=property_['setter'],
        initial_method_name=property_['initial'],
    )


def _create_inherited_flag_field(property_):
    """
    Create the field used for an inheritance fast path from an independent CSS property,
    and return the Field object.
    """
    name_for_methods = join_name(property_['name_for_methods'], 'is inherited')
    return Field(
        'inherited_flag',
        name_for_methods,
        property_name=property_['name'],
        type_name='bool',
        field_template='primitive',
        field_group=property_['field_group'],
        size=1,
        default_value='true',
        has_custom_compare_and_copy=False,
        getter_method_name=method_name(name_for_methods),
        setter_method_name=method_name(join_name('set', name_for_methods)),
        initial_method_name=method_name(join_name('initial', name_for_methods)),
    )


def _create_fields(properties):
    """
    Create ComputedStyle fields from properties and return a list of Field objects.
    """
    fields = []
    for property_ in properties:
        # Only generate properties that have a field template
        if property_['field_template'] is not None:
            # If the property is independent, add the single-bit sized isInherited flag
            # to the list of Fields as well.
            if property_['independent']:
                fields.append(_create_inherited_flag_field(property_))

            fields.append(_create_property_field(property_))

    return fields


def _reorder_bit_fields(bit_fields):
    # Since fields cannot cross word boundaries, in order to minimize
    # padding, group fields into buckets so that as many buckets as possible
    # are exactly 32 bits. Although this greedy approach may not always
    # produce the optimal solution, we add a static_assert to the code to
    # ensure ComputedStyleBase results in the expected size. If that
    # static_assert fails, this code is falling into the small number of
    # cases that are suboptimal, and may need to be rethought.
    # For more details on packing bit fields to reduce padding, see:
    # http://www.catb.org/esr/structure-packing/#_bitfields
    field_buckets = []
    # Consider fields in descending order of size to reduce fragmentation
    # when they are selected. Ties broken in alphabetical order by name.
    for field in sorted(bit_fields, key=lambda f: (-f.size, f.name)):
        added_to_bucket = False
        # Go through each bucket and add this field if it will not increase
        # the bucket's size to larger than 32 bits. Otherwise, make a new
        # bucket containing only this field.
        for bucket in field_buckets:
            if sum(f.size for f in bucket) + field.size <= 32:
                bucket.append(field)
                added_to_bucket = True
                break
        if not added_to_bucket:
            field_buckets.append([field])

    return _flatten_list(field_buckets)


def _reorder_non_bit_fields(non_bit_fields):
    # A general rule of thumb is to sort members by their alignment requirement
    # (from biggest aligned to smallest).
    for field in non_bit_fields:
        assert field.type_name in ALIGNMENT_ORDER, \
            "Type {} has unknown alignment. Please update ALIGNMENT_ORDER to include it.".format(field.type_name)
    return list(sorted(non_bit_fields, key=lambda f: ALIGNMENT_ORDER.index(f.type_name)))


def _reorder_fields(fields):
    """
    Returns a list of fields ordered to minimise padding.
    """
    # Separate out bit fields from non bit fields
    bit_fields = [field for field in fields if field.is_bit_field]
    non_bit_fields = [field for field in fields if not field.is_bit_field]

    # Non bit fields go first, then the bit fields.
    return _reorder_non_bit_fields(non_bit_fields) + _reorder_bit_fields(bit_fields)


class ComputedStyleBaseWriter(make_style_builder.StyleBuilderWriter):
    def __init__(self, json5_file_paths):
        # Read CSS properties
        super(ComputedStyleBaseWriter, self).__init__([json5_file_paths[0]])

        # Ignore shorthand properties
        for property_ in self._properties.values():
            if property_['field_template'] is not None:
                assert not property_['longhands'], \
                    "Shorthand '{}' cannot have a field_template.".format(property_['name'])

        css_properties = [value for value in self._properties.values() if not value['longhands']]

        for property_ in css_properties:
            # All CSS properties that are generated do not have custom comparison and copy logic.
            property_['has_custom_compare_and_copy'] = False

        # Read extra fields using the parameter specification from the CSS properties file.
        extra_fields = json5_generator.Json5File.load_from_files(
            [json5_file_paths[1]],
            default_parameters=self.json5_file.parameters
        ).name_dictionaries

        for property_ in extra_fields:
            make_style_builder.apply_property_naming_defaults(property_)

        all_properties = css_properties + extra_fields

        # Override the type name when field_type_path is specified
        for property_ in all_properties:
            if property_['field_type_path']:
                property_['type_name'] = property_['field_type_path'].split('/')[-1]

        self._generated_enums = _create_enums(all_properties)

        all_fields = _create_fields(all_properties)

        # Organise fields into a tree structure where the root group
        # is ComputedStyleBase.
        self._root_group = _group_fields(all_fields)
        self._diff_functions_map = _create_diff_groups_map(json5_generator.Json5File.load_from_files(
            [json5_file_paths[2]]
        ).name_dictionaries, self._root_group)

        self._include_paths = _get_include_paths(all_properties)
        self._outputs = {
            'ComputedStyleBase.h': self.generate_base_computed_style_h,
            'ComputedStyleBaseConstants.h': self.generate_base_computed_style_constants,
        }

    @template_expander.use_jinja('ComputedStyleBase.h.tmpl', tests={'in': lambda a, b: a in b})
    def generate_base_computed_style_h(self):
        return {
            'properties': self._properties,
            'enums': self._generated_enums,
            'include_paths': self._include_paths,
            'computed_style': self._root_group,
            'diff_functions_map': self._diff_functions_map,
        }

    @template_expander.use_jinja('ComputedStyleBaseConstants.h.tmpl')
    def generate_base_computed_style_constants(self):
        return {
            'properties': self._properties,
            'enums': self._generated_enums,
        }

if __name__ == '__main__':
    json5_generator.Maker(ComputedStyleBaseWriter).main()
