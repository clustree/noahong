# Copyright (C) 2012 Jeff Donner
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation files
# (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge,
# publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions: The above copyright notice and
# this permission notice shall be included in all copies or substantial
# portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
# WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
# TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
# ('expat' version of the MIT license)

# rm -f noaho.cpp && rm -rf build && rm -rf .objs && rm -f *.so && python3 cython_deep_build_setup.py build_ext --inplace

import os

from cpython.ref cimport Py_INCREF
from cpython.ref cimport Py_DECREF
from cpython.version cimport PY_MAJOR_VERSION
from libc.stdint cimport int32_t
from libcpp cimport bool as bool_t

cdef get_as_utf8(object text):
    if isinstance(text, unicode) or (PY_MAJOR_VERSION < 3 and isinstance(text, str)):
        utf8_data = text.encode('utf-8', errors='replace')
    else:
        raise ValueError("Requires unicode or str text input, got %s" % type(text))

    # http://wiki.cython.org/FAQ#HowdoIpassaPythonstringparameterontoaClibrary.3F
    return utf8_data, len(utf8_data)


cdef extern from "array-aho.h":
    cdef cppclass AhoCorasickTrie:
        AhoCorasickTrie()
# http://groups.google.com/group/cython-users/browse_thread/thread/d0c57ee21a278db7/ffb90297821483d8?lnk=gst&q=map+return+object#ffb90297821483d8
        void add_string(char* text, int len, int payload) except +AssertionError
        void compile()
        int find_short(char* text, int len,
                         int* out_start, int* out_end) except +AssertionError
        int find_longest(char* text, int len,
                         int* out_start, int* out_end) except +AssertionError
        int find_anchored(char* text, int len, char anchor,
                          int* out_start, int* out_end) except +AssertionError
        int num_keys()
        int num_nodes()
        int num_total_children()
        int get_payload(char* text, int len) except +AssertionError
        int contains(char* text, int len) except +AssertionError
        void write(char* path, int len) except +AssertionError


cdef extern from "array-aho.h":
    cdef cppclass Utf8CodePoints:
        Utf8CodePoints()
        void create(char* s, int n)
        int32_t get_codepoint_index(int32_t byte_index)


cdef extern from "array-aho.h":
    cdef cppclass MappedTrie:
        MappedTrie(char* path, int n) except +AssertionError
        int find_anchored(char* text, int len, char anchor,
                          int* out_start, int* out_end) except +AssertionError
        int num_nodes()

class PayloadWriteError(BaseException):
    pass

cdef class NoAho:
    cdef AhoCorasickTrie *thisptr
    cdef object payloads_to_decref
    cdef int has_noninteger_payload

    def __cinit__(self):
        self.thisptr = new AhoCorasickTrie()
        self.payloads_to_decref = []
        self.has_noninteger_payload = 0

    def __dealloc__(self):
        for payload in self.payloads_to_decref:
            Py_DECREF(payload)
        del self.thisptr

    def __len__(self):
        return self.thisptr.num_keys()

    def nodes_count(self):
        return self.thisptr.num_nodes()

    def children_count(self):
        return self.thisptr.num_total_children()

    def write(self, path):
        cdef bytes utf8_data
        cdef int num_utf8_chars
        utf8_data, num_utf8_chars = get_as_utf8(path)
        if self.has_noninteger_payload:
            raise PayloadWriteError("Cannot write a NoAho trie with non integer payload.")
        self.thisptr.write(utf8_data, num_utf8_chars)

    def __contains__(self, key_text):
        cdef bytes utf8_data
        cdef int num_utf8_chars
        utf8_data, num_utf8_chars = get_as_utf8(key_text)
        return self.thisptr.contains(utf8_data, num_utf8_chars)

    def __getitem__(self, text):
        cdef bytes utf8_data
        cdef int num_utf8_chars
        cdef int32_t payload_index
        utf8_data, num_utf8_chars = get_as_utf8(text)
        payload_index = self.thisptr.get_payload(utf8_data, num_utf8_chars)
        if payload_index < 0:
            raise KeyError(text)
        return self.payloads_to_decref[payload_index]

    def __setitem__(self, text, py_payload):
        self.add(text, py_payload)

# This is harder...
#    def __iter__(self):
#        return

    def add(self, text, py_payload = None):
        cdef bytes utf8_data
        cdef int num_utf8_chars
        cdef int32_t payload_index

        utf8_data, num_utf8_chars = get_as_utf8(text)

        if num_utf8_chars == 0:
            raise ValueError("Key cannot be empty (would cause Aho-Corasick automaton to spin)")

        payload_index = -1
        if not isinstance(py_payload, int):
            self.has_noninteger_payload = 1
        if py_payload is not None:
            Py_INCREF(py_payload)
            payload_index = len(self.payloads_to_decref)
            self.payloads_to_decref.append(py_payload)

        self.thisptr.add_string(utf8_data, num_utf8_chars, payload_index)

# Nice-to-have...
#    def add_many(self):
#        pass

    def compile(self):
        self.thisptr.compile()

    def find_short(self, text):
        cdef int start, end
        cdef bytes utf8_data
        cdef int num_utf8_chars
        cdef void* void_payload
        cdef int32_t payload_index
        cdef object py_payload
        cdef Utf8CodePoints code_points
        utf8_data, num_utf8_chars = get_as_utf8(text)
        start = 0
        end = 0
        payload_index = self.thisptr.find_short(utf8_data, num_utf8_chars,
                                                &start, &end)
        py_payload = None
        if payload_index >= 0:
            py_payload = self.payloads_to_decref[payload_index]
        if start == end:
            return None, None, None
        code_points.create(utf8_data, num_utf8_chars)
        start = code_points.get_codepoint_index(start)
        end = code_points.get_codepoint_index(end)
        return start, end, py_payload

    def find_long(self, text):
        cdef int start, end
        cdef bytes utf8_data
        cdef int num_utf8_chars
        cdef int32_t payload_index
        cdef object py_payload
        cdef Utf8CodePoints code_points
        utf8_data, num_utf8_chars = get_as_utf8(text)
        start = 0
        end = 0
        payload_index = self.thisptr.find_longest(utf8_data, num_utf8_chars,
                                                  &start, &end)
        py_payload = None
        if payload_index >= 0:
            py_payload = self.payloads_to_decref[payload_index]
        if start == end:
            return None, None, None
        code_points.create(utf8_data, num_utf8_chars)
        start = code_points.get_codepoint_index(start)
        end = code_points.get_codepoint_index(end)
        return start, end, py_payload

# http://thread.gmane.org/gmane.comp.python.cython.user/1920/focus=1921
    def findall_short(self, text):
        cdef bytes utf8_data
        cdef int num_utf8_chars
        utf8_data, num_utf8_chars = get_as_utf8(text)
        # 0 is flag for 'short'
        return AhoIterator(self, utf8_data, num_utf8_chars, 0)

    def findall_long(self, text):
        cdef bytes utf8_data
        cdef int num_utf8_chars
        utf8_data, num_utf8_chars = get_as_utf8(text)
        # 1 is flag for 'long'
        return AhoIterator(self, utf8_data, num_utf8_chars, 1)

    def findall_anchored(self, text):
        cdef bytes utf8_data
        cdef int num_utf8_chars
        utf8_data, num_utf8_chars = get_as_utf8(text)
        # 2 is flag for 'long'
        return AhoIterator(self, utf8_data, num_utf8_chars, 2)

    def payloads(self):
        return self.payloads_to_decref


# iterators (though, there was another, better one! -- try __citer__ and __cnext__)
# http://thread.gmane.org/gmane.comp.python.cython.user/2942/focus=2944

# http://groups.google.com/group/cython-users/browse_thread/thread/69b6eeb930826bcb/0b20e6e265e719a3?lnk=gst&q=iterator#0b20e6e265e719a3
cdef class AhoIterator:
    cdef NoAho aho_obj
    cdef Utf8CodePoints code_points
    cdef bytes utf8_data
    cdef int num_utf8_chars
    cdef int start, end, want_longest

    def __init__(self, aho_obj, utf8_data, num_utf8_chars, want_longest):
        self.aho_obj = aho_obj
        self.code_points = Utf8CodePoints()
        self.code_points.create(utf8_data, num_utf8_chars)
        self.utf8_data = utf8_data
        self.num_utf8_chars = num_utf8_chars
        self.start = 0
        self.end = 0
        self.want_longest = want_longest

    # I belieeeeve we don't need a __dealloc__ here, that Cython bumps
    # Python objects (self.aho_obj when I assign, and derefs them
    # when we die.

    def __iter__(self):
        return self

    def __next__(self):
        cdef int32_t payload_index
        cdef object py_payload
        cdef int out_start, out_end

        # I figured a runtime switch was worth not having 2
        # iterator types.
        if self.want_longest == 1:
            payload_index = self.aho_obj.thisptr.find_longest(
                self.utf8_data, self.num_utf8_chars,
                &self.start, &self.end)
        elif self.want_longest == 2:
            payload_index = self.aho_obj.thisptr.find_anchored(
                self.utf8_data, self.num_utf8_chars, 0x1F,
                &self.start, &self.end)
        else:
            payload_index = self.aho_obj.thisptr.find_short(
                self.utf8_data, self.num_utf8_chars,
                &self.start, &self.end)

        py_payload = None
        if payload_index >= 0:
            py_payload = self.aho_obj.payloads_to_decref[payload_index]
        if self.start < self.end:
            # set up for next time
            out_start = self.code_points.get_codepoint_index(self.start)
            out_end = self.code_points.get_codepoint_index(self.end)
            self.start = self.end
            return out_start, out_end, py_payload
        else:
            raise StopIteration


cdef class MappedIterator:
    cdef Mapped mapped
    cdef Utf8CodePoints code_points
    cdef bytes utf8_data
    cdef int num_utf8_chars
    cdef int start, end

    def __init__(self, mapped, utf8_data, num_utf8_chars):
        self.mapped = mapped
        self.code_points = Utf8CodePoints()
        self.code_points.create(utf8_data, num_utf8_chars)
        self.utf8_data = utf8_data
        self.num_utf8_chars = num_utf8_chars
        self.start = 0
        self.end = 0

    def __iter__(self):
        return self

    def __next__(self):
        cdef int32_t payload_index
        cdef object py_payload
        cdef int out_start, out_end

        payload_index = self.mapped.trie.find_anchored(
            self.utf8_data, self.num_utf8_chars, 0x1F,
            &self.start, &self.end)

        if self.start < self.end:
            # set up for next time
            out_start = self.code_points.get_codepoint_index(self.start)
            out_end = self.code_points.get_codepoint_index(self.end)
            self.start = self.end
            return out_start, out_end, payload_index
        else:
            raise StopIteration


cdef class Mapped:
    cdef MappedTrie *trie
    cdef bool_t closed

    def __cinit__(self, path):
        cdef bytes encoded_path
        cdef int num_chars
        encoded_path = os.fsencode(path)
        num_chars = len(encoded_path)
        self.trie = new MappedTrie(encoded_path, num_chars)
        self.closed = False

    def __dealloc__(self):
        if not self.closed:
            del self.trie

    def close(self):
        del self.trie
        self.closed = True

    def findall_anchored(self, text):
        cdef bytes utf8_data
        cdef int num_utf8_chars
        utf8_data, num_utf8_chars = get_as_utf8(text)
        return MappedIterator(self, utf8_data, num_utf8_chars)

    def nodes_count(self):
        return self.trie.num_nodes()
