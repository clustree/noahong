# Python Aho-Corasick implementation

`noahong` is a Python implementation of the [Aho-Corasick](https://en.wikipedia.org/wiki/Aho%E2%80%93Corasick_algorithm) algorithm for string matching, based on a fork of the [NoAho](https://github.com/JDonner/NoAho) C++ implementation.


`noahong` supports macOS, Linux, Windows on Python 3.6+.


## API

The first thing to do is to instantiate a `NoAho` object and add some keys to it (optionally with different payloads for each).

```python3
from noahong import NoAho

trie = NoAho()

# fill with .add()
trie.add("foo", "id_foo")
trie.add("foobar", "id_foobar")

# or fill with __setitem__
trie["bar"] = "id_bar"
```

Once you have added the different keys and their payloads, the `NoAho` object needs to be compiled:

```python3
trie.compile()
```

Once it is compiled, keys can no longer be added to the `NoAho` object.

`noahong` then exposes four functions to find matching substrings in text:

### `find_short`

`trie.find_short(text)` finds the first substring of `text` that is matched by a key added to the `trie`. 

It returns a tuple `(start, stop, payload)` such that: 
- `payload` is the object inserted with `trie.add()`
- `start` and `stop` are indices of the match in the `text`: `text[start:stop] == key` 

For example, using the above `trie`:

```python3
trie.find_short("something foo")
# returns (10, 13, 'id_foo')
# "something foo"[10:13] == "foo"
```

and returns the first match even though a longer match may start at the same position:

```python3
trie.find_short("something foobar")
# returns (10, 13, 'id_foo')
```

### `find_long`

`trie.find_long(text)` finds the first longest substring of `text` that is matched by a key added to the `trie`. 

For example, using the above `trie`:

```python3
trie.find_long("something foobar")
# returns (10, 16, 'id_foobar')
```

### `findall_*`

Both `find_short` and `find_long` have a `findall_short` and `findall_long` counterparts that allow you to iterate on all non-overlapping matches found
in the text:

```python3
for x in trie.findall_long("something foo bar foobar"): 
    print(x)       

# prints                          
# (10, 13, 'id_foo')
# (14, 17, 'id_bar')
# (18, 24, 'id_foobar')
```

Because matches are non-overlapping:

```python3
list(trie.findall_short("foobar")) == [(0, 3, "id_foo"), (3, 6, "id_bar")]
```

whereas:

```python3
list(trie.findall_long("foobar")) == [(0, 6, "id_foobar")]
```

### Payloads

`NoAho` tries accept any Python object as a payload:

```python3
trie = NoAho()
trie.add("foo", 0)
trie.add("bar", CustomClass())
trie.add("baz", lambda x: x)
```

The same payload can be associated with different keys.

Notice that the non-pickable `lambda x: x` payload works because
there is no serialization involved here.

### Length and inclusion

`NoAho` trie objects also expose the number of keys with `len`:

```python3
len(trie)
```

And, when they are compiled, they can be used to test for key inclusion:

```python3
"foo" in trie
```

The number of nodes in the underlying Trie can be recovered with 

```python3
trie.nodes_count()
```

## Mapped `NoAho`

In order to save memory, `noahong` exposes a `Mapped` matching object which can be written to disk and later loaded directly to memory to perform matches with a smaller memory footprint.

The `Mapped` object exposes different finding methods and only supports integer payloads.

Construct it by adding keys and payloads to a `NoAho` object:

```python3
from noahong import NoAho, Mapped

trie = NoAho()
trie.add("baz", "id_baz")

trie.compile()
trie.write("./test.matcher")

mapped_trie = Mapped("./test.matcher")
```

The `mapped_trie` object exposes a `findall_anchored` function that iterates over _anchored_ matches, matches that can be found within boundaries set with a special "anchor" character `\u001F`.

This is useful to restrict matches to be found only between, say, spaces:

```python3
trie = NoAho()
trie.add("foo", 0)
trie.add("bar", 1)

trie.compile()
trie.write("./test.matcher")

mapped_trie = Mapped("./test.matcher")
mapped_trie.findall_anchored("\u001Fbar\u001F\u001Ffoo\u001F\u001Ffoobar\u001F")

# returns [(1, 4, 1), (6, 9, 0), (11, 14, 0)]
```

Notice how `"bar"` is not found in the final `"foobar"` because it is not present between "anchor" characters.

It is possible to place anchor characters in the keys:

```python3
trie = NoAho()
trie.add("foo\u001F\u001Fbar", 0)
trie.add("foo", 1)
trie.add("bar", 2)

trie.compile()
trie.write("./test.matcher")

mapped_trie = Mapped("./test.matcher")
mapped_trie.findall_anchored("\u001Ffoo\u001F\u001Fbar\u001F")
# returns [(1, 9, 0)]
```

In this case, the longest key found between anchors is returned.


## Installation

### Devpi

`noahong` is available on devpi:

```
pip install noahong
```

### Python 3

`noahong` can be installed manually:

```
python3 setup.py install
```

## Legacy README

You can find more information on the package and C++ implementation by reading the 
legacy README found [here](./README-legacy.md).
