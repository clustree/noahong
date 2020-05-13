// Copyright (C) 2012, 2014 Jeff Donner
//
// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// (MIT 'expat' license)

// Description of Aho-Corasick:
//   http://www.cs.uku.fi/~kilpelai/BSA05/lectures/slides04.pdf

#include "array-aho.h"
#include <limits>
#include <queue>
#include <iostream>
#include <utility>
#include <iso646.h>

#include <sys/mman.h>
#include <fcntl.h>


using namespace std;


std::ostream& operator<<(std::ostream& os, Node const& node) {
   const Node::Children& children = node.get_children();
   for (Node::Children::const_iterator i = children.begin(),
           end = children.end();
        i != end; ++i) {
      os << (char)i->first << ';';
   }
   os << "failure: " << node.ifailure_state;
   os << std::endl;
   return os;
}


std::ostream& operator<<(std::ostream& os, AhoCorasickTrie::Chars const& text) {
   for (AhoCorasickTrie::Chars::const_iterator i = text.begin(), end = text.end();
        i != end; ++i) {
      os << (char)*i;
   }
   return os;
}


std::ostream& operator<<(std::ostream& os, AhoCorasickTrie::Strings const& texts) {
   for (AhoCorasickTrie::Strings::const_iterator i = texts.begin(), end = texts.end();
        i != end; ++i) {
      os << '[' << *i << ']' << endl;
   }
   return os;
}

typedef std::pair<int32_t, PayloadT> NodePayload;


FrozenNode::FrozenNode()
   : chars_offset(0)
   , ifailure_state(0)
   , chars_count(0)
   , length(0)
{}


Node::Index FrozenNode::child_at(const FrozenChars& chars,
        const FrozenIndices& indices, AC_CHAR_TYPE c) const {
  const FrozenChars::const_iterator begin = chars.begin() + chars_offset;
  const FrozenChars::const_iterator end = begin + chars_count;
  const FrozenChars::const_iterator child = std::lower_bound(begin, end, c);
  if (child == end || *child != c)
      // since these are indices, 0 is valid, so invalid is < 0
      return -1;
  return indices[child - chars.begin()];
}


namespace {

// find_anchored_in_trie find the longest, immediate anchored match in
// char_s[inout_istart:n]. Let's have anchor == ".", then char_s must be
// tokenized like:
//
//   .word1..word2..word3.. ... wordN.
//
// and the trie must be filled with entries like:
//
//   .word1.
//   .word2.
//   .word2..word3.
//
// find_anchored will find all closest matching entries and return the longest
// one. It takes advantage of FrozenTrie being a trie and ignores the suffix
// link information.
PayloadT find_anchored_in_trie(const AbstractTrie* trie,
                               char const* char_s, size_t n, char anchor,
                               int* inout_istart,
                               int* out_iend) {
   int length_longest = -1;
   int end_longest = -1;
   bool have_match = false;

   Node::Index inode = -1;
   AC_CHAR_TYPE const* original_start =
      reinterpret_cast<AC_CHAR_TYPE const*>(char_s);
   AC_CHAR_TYPE const* start = original_start + *inout_istart;
   AC_CHAR_TYPE const* end = original_start + n;

   for (;;) {
      // Find next anchor
      while (start < end && *start != anchor) {
         ++start;
      }
      if (start >= end)
         break;

      Node::Index istate = 0;
      for (AC_CHAR_TYPE const* c = start; c < end; ++c) {
         istate = trie->child_at(istate, *c);
         if (istate < 0)
            break;
         const auto n = trie->get_node(istate);
         int keylen = n.length;
         if (keylen && length_longest < keylen) {
            have_match = true;
            length_longest = keylen;
            end_longest = c + 1 - original_start;
            inode = istate;
         }
      }
      if (have_match) {
         *out_iend = end_longest;
         *inout_istart = *out_iend - length_longest;
         return trie->payload_at(inode);
      }
      ++start;
   }
   return -1;
}

}  // namespace


class FrozenTrie: public AbstractTrie {
public:
   typedef Node::Index Index;

   FrozenTrie(Nodes&, std::deque<unsigned short>& length);

   PayloadT find_short(char const* s, size_t n,
                       int* inout_start,
                       int* out_end) const;

   PayloadT find_longest(char const* s, size_t n,
                         int* inout_start,
                         int* out_end) const;

   PayloadT find_anchored(char const* s, size_t n, char anchor,
                          int* inout_start,
                          int* out_end) const;

   int contains(char const*, size_t n) const;

   int num_keys() const;

   int num_nodes() const;

   int num_total_children() const;

   /// Returns either a valid ptr (including 0) or, -1 cast as a ptr.
   PayloadT get_payload(char const* s, size_t n) const;

   void write(std::string path) const;

   virtual Index child_at(Index i, AC_CHAR_TYPE a) const;
   virtual PayloadT payload_at(Index i) const;
   virtual FrozenNode get_node(Node::Index i) const;
   virtual ~FrozenTrie() {}

private:
   // root is at 0 of course.
   typedef std::vector<FrozenNode> FrozenNodes;
   FrozenNodes nodes;
   FrozenChars chars;
   FrozenIndices indices;

   // Denormalizing payloads is a win because we often 10x more non-payload
   // nodes than payload ones, and payload entries are only 2x more expensive.
   std::vector<NodePayload> payloads;
};


namespace {

// Write native types to a file. No effort is made to ensure portability
// meaning source and destination architectures must be the same.
class Writer {
public:
    Writer(std::string path) {
        this->fp = fopen(path.c_str(), "wb");
        if (!this->fp) {
            throw std::runtime_error("failed to open file: " + std::string(path));
        }
    }

    ~Writer() {
        fclose(this->fp);
    }

    void write_int32(int32_t value) {
        this->write_bytes(&value, sizeof(value));
    }

    void write_int16(int16_t value) {
        this->write_bytes(&value, sizeof(value));
    }

    void write_unsigned_short(unsigned short value) {
        this->write_bytes(&value, sizeof(value));
    }

    void write_size_t(size_t value) {
        this->write_bytes(&value, sizeof(value));
    }

    void write_bytes(const void* data, size_t len) {
        const auto written = fwrite(data, 1, len, this->fp);
        if (written != len) {
            throw std::runtime_error("write failed");
        }
    }

private:
    FILE* fp;
};

typedef unsigned short bom_t;
const bom_t BOM = 0xBABB;

}  // namespace


void FrozenTrie::write(std::string path) const {
    // Serialize data structures as separate arrays. They will be more
    // expensive to read once mmapped because more pages will be touched but it
    // helps unifying the deserialization process and check all access to
    // mapped memory.
    Writer w(path);
    w.write_unsigned_short(BOM);
    w.write_size_t(this->nodes.size());
    for (const FrozenNode& n: this->nodes) {
        w.write_int32(n.chars_offset);
    }
    w.write_size_t(this->nodes.size());
    for (const FrozenNode& n: this->nodes) {
        w.write_int32(n.ifailure_state);
    }
    w.write_size_t(this->nodes.size());
    for (const FrozenNode& n: this->nodes) {
        w.write_int16(n.chars_count);
    }
    w.write_size_t(this->nodes.size());
    for (const FrozenNode& n: this->nodes) {
        w.write_unsigned_short(n.length);
    }

    w.write_size_t(this->chars.size());
    w.write_bytes(&(this->chars[0]), this->chars.size() * sizeof(AC_CHAR_TYPE));

    w.write_size_t(this->indices.size());
    for (int32_t i: this->indices) {
        w.write_int32(i);
    }

    w.write_size_t(this->payloads.size());
    for (NodePayload p: this->payloads) {
        w.write_int32(p.first);
    }
    w.write_size_t(this->payloads.size());
    for (NodePayload p: this->payloads) {
        w.write_int32(p.second);
    }
}


FrozenTrie::FrozenTrie(Nodes& source_nodes,
        std::deque<unsigned short>& source_length) {
   size_t payloads_count = 0;
   size_t chars_count = 0;
   for (size_t i = 0; i < source_nodes.size(); ++i)  {
      const auto& n = source_nodes[i];
      if (n.payload >= 0)
         ++payloads_count;
      chars_count += n.get_children().size();
   }
   payloads.resize(payloads_count);
   size_t payloads_index = 0;
   chars.resize(chars_count);
   size_t chars_index = 0;
   indices.resize(chars_count);
   nodes.resize(source_nodes.size());
   size_t nodes_index = 0;

   while (!source_nodes.empty()) {
      const Node& n = source_nodes.front();
      const Node::Children& n_children = n.get_children();
      FrozenNode f;
      f.length = source_length.front();
      f.ifailure_state = n.ifailure_state;
      f.chars_offset = chars_index;
      if (n_children.size() > std::numeric_limits<int16_t>::max())
         throw std::runtime_error("node children count overflow");
      f.chars_count = n_children.size();
      nodes[nodes_index++] = f;

      if (n.payload >= 0) {
         payloads[payloads_index++] = NodePayload(
                     static_cast<int32_t>(nodes_index - 1),
                     n.payload);
      }

      Node::Children::const_iterator it;
      for (it = n_children.begin(); it != n_children.end(); ++it) {
          chars[chars_index] = it->first;
          indices[chars_index++] = it->second;
      }

      source_nodes.pop_front();
      source_length.pop_front();
   }
}


Node::Index FrozenTrie::child_at(Index i, AC_CHAR_TYPE a) const {
    Index ichild = nodes[i].child_at(chars, indices, a);
    // The root is a special case - every char that's not an actual
    // child of the root, points back to the root.
    if (ichild < 0 && i == 0)
        ichild = 0;
    return ichild;
}


PayloadT FrozenTrie::payload_at(Index i) const {
    if (i <= 0)
        return -1;
    const auto it = std::lower_bound(payloads.begin(), payloads.end(),
            NodePayload(i, -1));
    if (it == payloads.end() || it->first != i)
        return -1;
    return it->second;
}


FrozenNode FrozenTrie::get_node(Node::Index i) const {
    return nodes.at(i);
}


PayloadT FrozenTrie::find_short(char const* char_s, size_t n,
                                int* inout_istart,
                                int* out_iend) const {
   Index istate = 0;
   AC_CHAR_TYPE const* original_start = reinterpret_cast<AC_CHAR_TYPE const*>(char_s);
   AC_CHAR_TYPE const* start = original_start + *inout_istart;
   AC_CHAR_TYPE const* end = original_start + n;

   for (AC_CHAR_TYPE const* c = start; c < end; ++c) {
      Index ichild = this->child_at(istate, *c);
      while (ichild < 0) {
         istate = nodes[istate].ifailure_state;
         ichild = this->child_at(istate, *c);
      }

      istate = ichild;
      if (nodes[istate].length and nodes[istate].length <= c + 1 - start) {
         *out_iend = c - original_start + 1;
         *inout_istart = *out_iend - nodes[istate].length;
         return payload_at(istate);
      }
   }
   return -1;
}


/*
 * <char_s> is the original material,
 * <n> its length.
 * <inout_istart> is offset from char_s, part of the caller's traversal
 *   state, to let zer get multiple matches from the same text.
 * <out_iend> one-past-the-last offset from <char_s> of /this/ match, the
 *   mate to <inout_istart>.
 *
 * Does not itself assure that the match bounds point to nothing (ie end ==
 * start) when nothing is found. The caller must do that (and the Python
 * interface, and the gtests do that.
 *
 * When there are multiple contiguous terminal nodes (keywords that end at some
 * spot) multiple calls of this will be O(n^2) in that contiguous length - it
 * looks through all contiguous matches to find the longest one before returning
 * anything.
 */
PayloadT FrozenTrie::find_longest(char const* char_s, size_t n,
                                  int* inout_istart,
                                  int* out_iend) const {
   // longest terminal length, among a contiguous bunch of terminals.
   int length_longest = -1;
   int end_longest = -1;
   Index istate = 0;
   bool have_match = false;

   Index inode = -1;
   AC_CHAR_TYPE const* original_start =
      reinterpret_cast<AC_CHAR_TYPE const*>(char_s);
   AC_CHAR_TYPE const* start = original_start + *inout_istart;
   AC_CHAR_TYPE const* end = original_start + n;

   for (AC_CHAR_TYPE const* c = start; c < end; ++c) {
      Index ichild = this->child_at(istate, *c);
      while (ichild < 0) {
         if (have_match) {
            goto success;
         }
         istate = nodes[istate].ifailure_state;
         ichild = this->child_at(istate, *c);
      }

      istate = ichild;
      int keylen = nodes[istate].length;
      if (keylen &&
          // not sure this 2nd condition is necessary
          keylen <= c + 1 - start &&
          length_longest < keylen) {
         have_match = true;

         length_longest = keylen;
         end_longest = c + 1 - original_start;
         inode = istate;
      }
   }
   if (have_match) {
success:
      *out_iend = end_longest;
      *inout_istart = *out_iend - length_longest;
   }
   return payload_at(inode);
}


PayloadT FrozenTrie::find_anchored(char const* char_s, size_t n, char anchor,
                                   int* inout_istart,
                                   int* out_iend) const {
   return find_anchored_in_trie(this, char_s, n, anchor, inout_istart, out_iend);
}


int FrozenTrie::contains(char const* char_s, size_t n) const {
   AC_CHAR_TYPE const* c = reinterpret_cast<AC_CHAR_TYPE const*>(char_s);
   Index inode = 0;
   for (size_t i = 0; i < n; ++i, ++c) {
      inode = nodes[inode].child_at(chars, indices, *c);
      if (inode < 0) {
         return 0;
      }
   }

   return nodes[inode].length ? 1 : 0;
}


int FrozenTrie::num_keys() const {
   int num = 0;
   for (FrozenNodes::const_iterator it = nodes.begin(), end = nodes.end();
        it != end; ++it) {
      if (it->length)
         ++num;
   }

   return num;
}


int FrozenTrie::num_total_children() const {
   return chars.size();
}


int FrozenTrie::num_nodes() const {
   return nodes.size();
}


PayloadT FrozenTrie::get_payload(char const* s, size_t n) const {
   AC_CHAR_TYPE const* utf8 = (AC_CHAR_TYPE const*)s;
   AC_CHAR_TYPE const* u = utf8;

   Node::Index inode = 0;
   for (u = utf8; u < utf8 + n; ++u) {
      inode = nodes[inode].child_at(chars, indices, *u);
      if (inode < 0)
         return (PayloadT)-1;
   }
   if (nodes[inode].length)
      return payload_at(inode);
   return (PayloadT)-1;
}


AhoCorasickTrie::AhoCorasickTrie() {
    // born with root node
    add_node();
}


AhoCorasickTrie::~AhoCorasickTrie() {
}


void AhoCorasickTrie::add_string(char const* char_s, size_t n,
                                 PayloadT payload) {

   if(frozen)
      throw std::runtime_error("cannot add entry to compiled trie");

   AC_CHAR_TYPE const* c = reinterpret_cast<AC_CHAR_TYPE const*>(char_s);

   Index iparent = 0;
   Index ichild = 0;
   for (size_t i = 0; i < n; ++i, ++c) {
      // Don't need the node here, it's behind the scenes.
      // on the other hand we don't care about the speed of adding
      // strings.
      ichild = nodes[iparent].child_at(*c);
      if (not is_valid(ichild)) {
         ichild = add_node();
         nodes[iparent].set_child(*c, ichild);
      }
      iparent = ichild;
   }
   nodes[ichild].payload = payload;
   lengths[ichild] = n;
}


int AhoCorasickTrie::contains(char const* char_s, size_t n) const {
   assert_compiled();
   return frozen->contains(char_s, n);
}


int AhoCorasickTrie::num_keys() const {
   if (frozen)
       return frozen->num_keys();
   int num = 0;
   for (size_t i = 0; i < lengths.size(); ++i)
      if (lengths[i])
         ++num;
   return num;
}


int AhoCorasickTrie::num_total_children() const {
   if (frozen)
       return frozen->num_total_children();
   int num = 0;
   for (Nodes::const_iterator it = nodes.begin(), end = nodes.end();
        it != end; ++it) {
      num += it->get_children().size();
   }

   return num;
}


int AhoCorasickTrie::num_nodes() const {
   if (frozen)
       return frozen->num_nodes();
   return nodes.size();
}


void AhoCorasickTrie::compile() {
   if (frozen)
      return;
   make_failure_links();
   frozen.reset(new FrozenTrie(nodes, lengths));
}


PayloadT AhoCorasickTrie::get_payload(char const* s, size_t n) const {
   assert_compiled();
   return frozen->get_payload(s, n);
}


// After:
//   http://www.quretec.com/u/vilo/edu/2005-06/Text_Algorithms/index.cgi?f=L2_Multiple_String&p=ACpre
void AhoCorasickTrie::make_failure_links() {
   queue<Node*> q;
   const Node::Children& children = nodes[Index(0)].get_children();
   for (Node::Children::const_iterator i = children.begin(),
           end = children.end();
        i != end; ++i) {
      Node* child = &nodes[i->second];
      q.push(child);
      child->ifailure_state = (Index)0;
   }
   // root fails to root
   nodes[0].ifailure_state = 0;

   while (not q.empty()) {
      Node* r = q.front();
      q.pop();
      const Node::Children& children = r->get_children();
      for (Node::Children::const_iterator is = children.begin(),
              end = children.end();
           is != end; ++is) {
         AC_CHAR_TYPE a = is->first;
         Node* s = &nodes[is->second];
         q.push(s);
         Index ifail_state = r->ifailure_state;
         Index ifail_child = this->child_at(ifail_state, a);
         while (not is_valid(ifail_child)) {
            ifail_state = nodes[ifail_state].ifailure_state;
            ifail_child = this->child_at(ifail_state, a);
         }
         s->ifailure_state = ifail_child;
      }
   }
}


bool AhoCorasickTrie::is_valid(Index ichild) {
   return 0 <= ichild;
}


void AhoCorasickTrie::assert_compiled() const {
   if (!frozen)
       throw std::runtime_error("trie must be compiled before use");
}


Node::Index AhoCorasickTrie::child_at(Index i, AC_CHAR_TYPE a) const {
    Index ichild = nodes[i].child_at(a);
    // The root is a special case - every char that's not an actual
    // child of the root, points back to the root.
    if (ichild < 0 && i == 0)
        ichild = 0;
    return ichild;
}


Node::Index AhoCorasickTrie::add_node() {
    nodes.push_back(Node());
    lengths.push_back(0);
    return nodes.size() - 1;
}


PayloadT AhoCorasickTrie::find_short(char const* char_s, size_t n,
                                     int* inout_istart,
                                     int* out_iend) const {
   assert_compiled();
   return frozen->find_short(char_s, n, inout_istart, out_iend);
}

/*
 * <char_s> is the original material,
 * <n> its length.
 * <inout_istart> is offset from char_s, part of the caller's traversal
 *   state, to let zer get multiple matches from the same text.
 * <out_iend> one-past-the-last offset from <char_s> of /this/ match, the
 *   mate to <inout_istart>.
 *
 * Does not itself assure that the match bounds point to nothing (ie end ==
 * start) when nothing is found. The caller must do that (and the Python
 * interface, and the gtests do that.
 *
 * When there are multiple contiguous terminal nodes (keywords that end at some
 * spot) multiple calls of this will be O(n^2) in that contiguous length - it
 * looks through all contiguous matches to find the longest one before returning
 * anything.
 */
PayloadT AhoCorasickTrie::find_longest(char const* char_s, size_t n,
                                       int* inout_istart,
                                       int* out_iend) const {
   assert_compiled();
   return frozen->find_longest(char_s, n, inout_istart, out_iend);
}


PayloadT AhoCorasickTrie::find_anchored(char const* char_s, size_t n, char anchor,
                                        int* inout_istart,
                                        int* out_iend) const {
   assert_compiled();
   return frozen->find_anchored(char_s, n, anchor, inout_istart, out_iend);
}


// For debugging.
void AhoCorasickTrie::print() const {
   typedef pair<AC_CHAR_TYPE, Index> Pair;
   queue<Pair> q;
   q.push(make_pair((AC_CHAR_TYPE)'@', 0));
   while (not q.empty()) {
      Pair p = q.front();
      q.pop();
      AC_CHAR_TYPE f = p.first;
      if (f == '$') {
         cout << endl;
         continue;
      } else {
         cout << (char)p.first << " ";
      }
      Index inode = p.second;
      if (is_valid(inode)) {
         const Node::Children& children = nodes[inode].get_children();
         for (Node::Children::const_iterator i = children.begin(),
                 end = children.end();
              i != end; ++i) {
            // structurally the same; will it work?
            q.push(make_pair(i->first, i->second));
         }
         // mark level
         q.push(make_pair<AC_CHAR_TYPE, Index>('$', 0));
      }
   }
}


void AhoCorasickTrie::write(char const* path, size_t n) const {
    assert_compiled();
    this->frozen->write(std::string(path, n));
}


Utf8CodePoints::Utf8CodePoints() {
}


void Utf8CodePoints::create(const char* s, size_t n) {
    indices.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        // Ignore byte starting by 01xxxxxx, the other ones are either ascii or
        // utf-8 sequence leaders.
        if ((s[i] & 0xC0) != 0x80) {
            indices.push_back(i);
        }
    }
}


int32_t Utf8CodePoints::get_codepoint_index(int byte_index) const {
    const auto pos = static_cast<int32_t>(byte_index);
    const auto it = std::lower_bound(indices.begin(), indices.end(), pos);
    return static_cast<int32_t>(it - indices.begin());
}


// Check access to an array of primitive values starting at base. The memory
// block starts with the number of elements as a size_t, followed by packed
// elements.
template<typename Type>
class MappedArray {
public:
    MappedArray(const uint8_t* base) {
        size_ = *reinterpret_cast<const size_t*>(base);
        begin_ = reinterpret_cast<const Type*>(base + sizeof(size_));
    }

    size_t size() const {
        return size_;
    }

    const Type* ptr(size_t index) const {
        if (index > size_) {
            throw std::runtime_error("ptr out of range");
        }
        return begin_ + index;
    }

    const uint8_t* end_bytes() const {
        return reinterpret_cast<const uint8_t*>(begin_ + size_);
    }

    Type get(size_t index) const {
        return *this->ptr(index);
    }

private:
    const Type* begin_;
    const Type* end_;
    size_t size_;
};


MappedTrie::MappedTrie(char* const path, size_t n)
    : fd(0)
    , mapped(0)
    , mapped_size(0) {

    std::string p(path, n);
#ifdef _WIN32
    int len = MultiByteToWideChar(CP_UTF8, 0, path, n, NULL, 0);
    std::wstring wp(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, path, n, &wp[0], len);
    this->fd = _wopen(wp.c_str(), _O_RDONLY, _S_IREAD);
#else
    this->fd = open(p.c_str(), O_RDONLY, static_cast<mode_t>(0400));
#endif
    if (this->fd < 0) {
        throw std::runtime_error("failed to open file: " + p);
    }
    this->mapped_size = lseek(this->fd, 0, SEEK_END);
    this->mapped= static_cast<const uint8_t*>(
            mmap(0, this->mapped_size, PROT_READ, MAP_SHARED, this->fd, 0));

    if (static_cast<size_t>(this->mapped_size) < sizeof(BOM)) {
        throw std::runtime_error("BOM is missing");
    }
    const bom_t bom = *reinterpret_cast<const bom_t*>(this->mapped);
    if (bom != BOM) {
        throw std::runtime_error("BOM does not match");
    }
    const auto mapped = this->mapped + sizeof(bom);

    this->nodes_chars_offset.reset(new MappedArray<int32_t>(mapped));
    this->nodes_ifailure_state.reset(new MappedArray<int32_t>(
        this->nodes_chars_offset->end_bytes()));
    this->nodes_chars_count.reset(new MappedArray<int16_t>(
        this->nodes_ifailure_state->end_bytes()));
    this->nodes_length.reset(new MappedArray<unsigned short>(
        this->nodes_chars_count->end_bytes()));
    this->chars.reset(new MappedArray<AC_CHAR_TYPE>(this->nodes_length->end_bytes()));
    this->indices.reset(new MappedArray<int32_t>(this->chars->end_bytes()));
    this->payload_keys.reset(new MappedArray<int32_t>(this->indices->end_bytes()));
    this->payload_values.reset(new MappedArray<int32_t>(this->payload_keys->end_bytes()));

    const size_t read = this->payload_values->end_bytes() - this->mapped;
    if (read != static_cast<size_t>(this->mapped_size)) {
        throw std::runtime_error("mmapped size does not match read bytes count");
    }
}


MappedTrie::~MappedTrie() {
    if (this->fd > 0) {
        close(this->fd);
    }
    if (this->mapped) {
        munmap((void*)this->mapped, this->mapped_size);
    }
}


FrozenNode MappedTrie::get_node(Node::Index i) const {
    if (i < 0) {
        throw std::runtime_error("invalid negative index");
    }
    const size_t index = static_cast<size_t>(i);
    FrozenNode n;
    n.chars_offset = this->nodes_chars_offset->get(index);
    n.ifailure_state = this->nodes_ifailure_state->get(index);
    n.chars_count = this->nodes_chars_count->get(index);
    n.length = this->nodes_length->get(index);
    return n;
}


Node::Index MappedTrie::child_index(Node::Index i, AC_CHAR_TYPE c) const {
    const auto n = this->get_node(i);
    const auto* begin = this->chars->ptr(n.chars_offset);
    const auto* end = this->chars->ptr(n.chars_offset + n.chars_count);
    const auto* child = std::lower_bound(begin, end, c);
    if (child == end || *child != c) {
        return -1;
    }
    size_t offset = child - this->chars->ptr(0);
    return this->indices->get(offset);
}


Node::Index MappedTrie::child_at(Node::Index i, AC_CHAR_TYPE c) const {
    auto ichild = this->child_index(i, c);
    // The root is a special case - every char that's not an actual
    // child of the root, points back to the root.
    if (ichild < 0 && i == 0) {
        ichild = 0;
    }
    return ichild;
}


PayloadT MappedTrie::find_anchored(char const* char_s, size_t n, char anchor,
                                   int* inout_istart,
                                   int* out_iend) const {
   return find_anchored_in_trie(this, char_s, n, anchor, inout_istart, out_iend);
}


PayloadT MappedTrie::payload_at(Node::Index i) const {
    if (i <= 0)
        return -1;
    const auto begin = this->payload_keys->ptr(0);
    const auto end = this->payload_keys->ptr(this->payload_keys->size());
    const auto it = std::lower_bound(begin, end, i);
    if (it == end || *it != i)
        return -1;
    return this->payload_values->get(it - begin);
}


int MappedTrie::num_nodes() const {
    return static_cast<int>(nodes_length->size());
}
