# !/usr/bin/env python
"""Unit tests for Aho Corasick keyword string searching.
Many tests copyright Danny Yoo, the rest copyright Jeff Donner.
Because I think Danny's tests were GPL, so, now, is this whole file.
So, if you distribute this test code you must also distribute .. uh,
this test code (this doesn't apply to the whole package).
Jeff Donner, jeffrey.donner@gmail.com
"""

import contextlib
import os
import tempfile

import pytest

from noahong import Mapped, NoAho, PayloadWriteError


def test_compile_before_use():
    tree = NoAho()
    tree.add("bar")
    # Cannot be used before compilation
    with pytest.raises(AssertionError):
        tree.find_short("xxxbaryyy")
    tree.compile()
    tree.find_short("xxxbaryyy")
    # Cannot add after compilation
    with pytest.raises(AssertionError):
        tree.add("foo")


def test_keyword_as_prefix_of_another():
    """According to John, there's a problem with the matcher.
    this test case should expose the bug."""
    tree = NoAho()
    tree.add("foobar")
    tree.add("foo")
    tree.add("bar")
    tree.compile()
    assert (3, 6, None) == tree.find_short("xxxfooyyy")
    assert (0, 3, None) == tree.find_short("foo")
    assert (3, 6, None) == tree.find_short("xxxbaryyy")


def test_another_find():
    """Just to triangulate the search code.  We want to make sure
    that the implementation can do more than one search, at
    least."""
    tree = NoAho()
    tree.add("Python")
    tree.add("PLT Scheme")
    tree.compile()
    assert (19, 25, None) == tree.find_short(
        "I am learning both Python and PLT Scheme"
    )  # NOQA
    assert (0, 10, None) == tree.find_short(
        "PLT Scheme is an interesting language."
    )  # NOQA


def test_simple_construction():
    tree = NoAho()
    tree.add("foo")
    tree.add("bar")
    tree.compile()
    (10, 13, None) == tree.find_short("this is a foo message")
    tree.children_count() == 6


def test_counts():
    tree = NoAho()
    tree.add("foo")
    tree.compile()
    assert tree.nodes_count() == 4
    assert tree.children_count() == 3

    tree = NoAho()
    tree.add("foo")
    tree.add("bar")
    tree.compile()
    assert tree.nodes_count() == 7
    assert tree.children_count() == 6

    tree = NoAho()
    tree.add("fo")
    tree.add("foo")
    tree.compile()
    assert tree.nodes_count() == 4
    assert tree.children_count() == 3


def test_find_longest():
    tree = NoAho()
    tree.add("a")
    tree.add("alphabet")
    tree.compile()
    assert (0, 1, None) == tree.find_short("alphabet soup")
    assert (0, 8, None) == tree.find_long("alphabet soup")
    assert (13, 14, None) == tree.find_long(
        "yummy, I see an alphabet soup bowl"
    )  # NOQA


def test_find_with_whole_match():
    """Make sure that longest search will match the whole string."""
    tree = NoAho()
    longString = "supercalifragilisticexpialidocious"
    tree.add(longString)
    tree.compile()
    assert (0, len(longString), None) == tree.find_short(longString)


def test_find_longest_with_whole_match():
    """Make sure that longest search will match the whole string."""
    tree = NoAho()
    longString = "supercalifragilisticexpialidocious"
    tree.add(longString)
    tree.compile()
    assert (0, len(longString), None) == tree.find_long(longString)


def test_find_longest_with_no_match():
    tree = NoAho()
    tree.add("foobar")
    tree.compile()
    assert (None, None, None) == tree.find_long("fooba")


def test_with_expected_non_match():
    """Check to see that we don't always get a successful match."""
    tree = NoAho()
    tree.add("wise man")
    tree.compile()
    assert (None, None, None) == tree.find_short(
        "where fools and wise men fear to tread"
    )


def test_reject_empty_key():
    tree = NoAho()
    with pytest.raises(ValueError):
        tree.add("")


def test_empty_construction():
    """Make sure that we can safely construct and dealloc a tree
    with no initial keywords.  Important because the C
    implementation assumes keywords exist on its dealloc, so we
    have to do some work on the back end to avoid silly segmentation
    errors."""
    tree = NoAho()
    del tree


def test_embedded_nulls():
    """Check to see if we can accept embedded nulls"""
    tree = NoAho()
    tree.add("hell\0 world")
    tree.compile()
    assert (None, None, None) == tree.find_short("ello\0 world")
    assert (0, 11, None) == tree.find_short("hell\0 world")


def test_embedded_nulls_again():
    tree = NoAho()
    tree.add("\0\0\0")
    tree.compile()
    assert (0, 3, None) == tree.find_short("\0\0\0\0\0\0\0\0")


def test_findall_and_findall_longest():
    tree = NoAho()
    tree.add("python")
    tree.add("perl")
    tree.add("scheme")
    tree.add("java")
    tree.add("pythonperl")
    tree.compile()
    assert [
        (0, 6, None),
        (6, 10, None),
        (10, 16, None),
        (16, 20, None),
    ] == list(  # NOQA
        tree.findall_short("pythonperlschemejava")
    )
    assert [(0, 10, None), (10, 16, None), (16, 20, None)] == list(
        tree.findall_long("pythonperlschemejava")
    )
    assert [] == list(tree.findall_short("no pascal here"))
    assert [] == list(tree.findall_long("no pascal here"))


def test_bug2_competing_longests():
    """Previously we'd return the /last/ key found, now we look forward
    while there are contiguous candidate keys, and actually return the
    longest.
    """
    tree = NoAho()
    tree.add("cisco", "cisco")
    tree.add("em", "em")
    tree.add("cisco systems australia", "cisco systems")
    tree.compile()
    assert [(0, 5, "cisco"), (10, 12, "em")] == list(
        tree.findall_long("cisco systems")
    )  # NOQA


def test_bug3_false_terminal_nodes():
    tree = NoAho()
    tree.add("an", None)
    tree.add("canal", None)
    tree.add("e can oilfield", None)
    tree.compile()
    assert [(4, 4 + 5, None)], list(tree.findall_long("one canal"))


def test_payload():
    tree = NoAho()

    class RandomClass(object):
        def __init__(self):
            pass

    obj = RandomClass()
    tree.add("python", "yes-python")
    tree.add("perl", "")
    tree.add("scheme", None)
    tree.add("lisp", [1, 2, 3])
    # no payload, comes out None
    tree.add("C++")
    tree.add("dylan", obj)
    tree.compile()

    assert (0, 6, "yes-python") == tree.find_short("python")
    assert (0, 4, "") == tree.find_short("perl")
    assert (0, 6, None) == tree.find_short("scheme")
    assert (0, 4, [1, 2, 3]) == tree.find_short("lisp")
    assert (0, 3, None) == tree.find_short("C++")
    assert (0, 5, obj) == tree.find_short("dylan")


def test_dict_style_get_and_set():
    tree = NoAho()
    tree["foo"] = 5
    tree.compile()
    assert 5 == tree["foo"]


def test_dict_style_set_empty_key():
    tree = NoAho()
    with pytest.raises(ValueError):
        tree[""] = None


def test_dict_style_set_nonstring_key():
    tree = NoAho()
    with pytest.raises(ValueError):
        tree[6] = None

    with pytest.raises(ValueError):
        tree[None] = None

    with pytest.raises(ValueError):
        tree[[]] = None


def test_dict_style_get_unseen_key():
    tree = NoAho()
    tree.compile()
    with pytest.raises(KeyError):
        tree["unseen"]
    with pytest.raises(KeyError):
        tree[""]


def test_dict_style_containment():
    tree = NoAho()
    tree["foo"] = 5
    tree.compile()
    assert "foo" in tree
    assert "" not in tree
    assert "fo" not in tree
    assert "o" not in tree
    assert "oo" not in tree
    assert "f" not in tree


def test_dict_style_len():
    tree = NoAho()
    tree["a"] = None
    tree["b"] = [1, 2]
    tree["c"] = 12
    tree.compile()
    assert 3 == len(tree)


# reminder that we need to figure out which version we're in, and
# test Python 2 unicode explicitly
@pytest.mark.xfail
def test_unicode_in_python2():
    assert False


# key iteration is unimplemented
@pytest.mark.xfail
def test_iteration():
    tree = NoAho()
    tree.add("Harry")
    tree.add("Hermione")
    tree.add("Ron")
    assert set("Harry", "Hermione", "Ron") == set(tree.keys())


# reminder that we need to implement findall_short
@pytest.mark.xfail
def test_subset():
    tree = NoAho()
    tree.add("he")
    tree.add("hers")
    assert [(0, 2, None), (0, 4, None)] == list(tree.findall_short("hers"))


def anchor(s):
    return s.replace(".", "\u001F")


def test_utf8():
    tree = NoAho()
    tree.add("étable")
    tree.add("béret")
    tree.add("blé")
    tree.compile()
    matches = list(tree.findall_long("étable béret blé"))
    assert matches == [(0, 6, None), (7, 12, None), (13, 16, None)]


def test_anchored():
    tree = NoAho()
    tree.add(anchor(".a..b..c."))
    tree.add(anchor(".b."))
    tree.compile()
    matches = list(tree.findall_anchored(anchor(".a..b..z.")))
    assert matches == [(3, 6, None)]


def test_mapped_trie():
    tree = NoAho()
    tree.add(anchor(".a..b..c."), 0)
    tree.add(anchor(".b."), 1)
    tree.add(anchor(".a..c."), 2)
    tree.add(anchor(".a..b."), 3)
    tree.add(anchor(".é."), 4)
    tree.compile()
    with tempfile.TemporaryDirectory(prefix="noahong-") as tmpdir:
        path = os.path.join(tmpdir, "mapped")
        tree.write(path)

        with contextlib.closing(Mapped(path)) as m:
            assert m.nodes_count() == tree.nodes_count()
            matches = list(m.findall_anchored(anchor(".a..b..c.")))
            assert matches == [(0, 9, 0)]
            matches = list(m.findall_anchored(anchor(".b.")))
            assert matches == [(0, 3, 1)]
            matches = list(m.findall_anchored(anchor(".a..c.")))
            assert matches == [(0, 6, 2)]
            matches = list(m.findall_anchored(anchor(".z.")))
            assert matches == []
            matches = list(m.findall_anchored(anchor(".z..a..b..z.")))
            assert matches == [(3, 9, 3)]
            matches = list(m.findall_anchored(anchor(".é.")))
            assert matches == [(0, 3, 4)]


def test_empty_mapped_trie():
    tree = NoAho()
    tree.compile()
    with tempfile.TemporaryDirectory(prefix="noahong-") as tmpdir:
        path = os.path.join(tmpdir, "mapped")
        tree.write(path)

        with contextlib.closing(Mapped(path)) as m:
            assert m.nodes_count() == 1
            assert m.nodes_count() == tree.nodes_count()
            matches = list(m.findall_anchored(anchor(".a..b..c.")))
            assert matches == []


def test_bad_mapped_trie():
    with tempfile.TemporaryDirectory(prefix="noahong-") as tmpdir:
        path = os.path.join(tmpdir, "mapped")

        # Input file is too short
        with open(path, "wb") as fp:
            fp.write(b"1")
        with pytest.raises(AssertionError):
            Mapped(path)

        # Invalid BOM
        with open(path, "wb") as fp:
            fp.write(b"1234")

        with pytest.raises(AssertionError):
            Mapped(path)

def test_mapped_trie_payload():
    trie = NoAho()
    trie.add("foo", None)
    trie.compile()
    with tempfile.TemporaryDirectory(prefix="noahong-") as tmpdir:
        path = os.path.join(tmpdir, "mapped")
        with pytest.raises(PayloadWriteError):
            trie.write(path)