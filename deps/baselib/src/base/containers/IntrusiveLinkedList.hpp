#pragma once
#include <base/ClassTraits.hpp>
#include <base/Error.hpp>

#include <memory>

namespace base::ll {

enum class IntrusiveListType {
  Owning,
  NonOwning,
};

template <typename T>
concept IntrusiveListTraits = requires(T) {
  { T::list_type } -> std::convertible_to<IntrusiveListType>;
};

template <IntrusiveListTraits Traits>
class IntrusiveNode;

template <IntrusiveListTraits Traits>
class IntrusiveLinkedList;

template <IntrusiveListTraits Traits>
class IntrusiveNode {
  using T = typename Traits::Item;
  using Owner = typename Traits::Owner;

  friend class IntrusiveLinkedList<Traits>;

  Owner* owner_ = nullptr;
  IntrusiveNode* next_ = nullptr;
  IntrusiveNode* previous_ = nullptr;

  IntrusiveLinkedList<Traits>& list() {
    verify(owner_, "cannot get containing list for unlinked node");
    return Traits::list_from_owner(owner_);
  }

  T* to_underlying() { return static_cast<T*>(this); }

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(IntrusiveNode);

  IntrusiveNode() = default;

  virtual ~IntrusiveNode() {
    if constexpr (Traits::list_type == IntrusiveListType::Owning) {
      verify(!owner_, "tried to destroy linked node");
    }
  }

  Owner* owner() { return owner_; }
  T* previous() { return static_cast<T*>(previous_); }
  T* next() { return static_cast<T*>(next_); }

  const Owner* owner() const { return owner_; }
  const T* previous() const { return static_cast<const T*>(previous_); }
  const T* next() const { return static_cast<const T*>(next_); }

 protected:
  void insert_before(T* before) { before->list().insert_before(to_underlying(), before); }
  void insert_after(T* after) { after->list().insert_after(to_underlying(), after); }

  void push_front(Owner* new_owner) { new_owner->list().push_front(to_underlying()); }
  void push_back(Owner* new_owner) { new_owner->list().push_back(to_underlying()); }

  void move_before(T* before) {
    unlink();
    insert_before(before);
  }

  void move_after(T* after) {
    unlink();
    insert_after(after);
  }

  void move_to_front(Owner* new_owner) {
    unlink();
    push_front(new_owner);
  }

  void move_to_back(Owner* new_owner) {
    unlink();
    push_back(new_owner);
  }

  void unlink() { list().unlink(to_underlying()); }

  void destroy() {
    if constexpr (Traits::list_type == IntrusiveListType::Owning) {
      if (owner_) {
        unlink();
      }

      delete this;
    } else {
      fatal_error("cannot call destroy() on non-owning list");
    }
  }
};

template <IntrusiveListTraits Traits>
class IntrusiveLinkedList {
  using T = typename Traits::Item;
  using Owner = typename Traits::Owner;

  template <typename TValue, typename TNode, bool Reversed>
  class IteratorInternal {
    TNode* node;

   public:
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = TValue;
    using pointer = value_type*;
    using reference = value_type&;

    explicit IteratorInternal(std::nullptr_t) : node(nullptr) {}
    explicit IteratorInternal(TValue* value) : node(static_cast<TNode*>(value)) {}
    explicit IteratorInternal(TNode* node) : node(node) {}

    IteratorInternal& operator++() {
      node = Reversed ? node->previous() : node->next();
      return *this;
    }

    IteratorInternal operator++(int) {
      const auto before = *this;
      ++(*this);
      return before;
    }

    IteratorInternal& operator--() {
      node = Reversed ? node->next() : node->previous();
      return *this;
    }

    IteratorInternal operator--(int) {
      const auto before = *this;
      --(*this);
      return before;
    }

    reference operator*() const { return static_cast<reference>(*node); }
    pointer operator->() { return static_cast<pointer>(node); }

    bool operator==(const IteratorInternal& rhs) const { return node == rhs.node; }
    bool operator!=(const IteratorInternal& rhs) const { return node != rhs.node; }
  };

  using Node = IntrusiveNode<Traits>;

  Owner* owner_ = nullptr;
  Node* head_ = nullptr;
  Node* tail_ = nullptr;

  size_t size_ = 0;

  Node* to_node(T* node) { return static_cast<Node*>(node); }

  void own_node(Node* node) {
    verify(node, "cannot own null node");
    verify(!node->owner_, "node is already owned");

    node->owner_ = owner_;

    size_++;

    Traits::on_node_added(owner_, static_cast<T*>(node));
  }

 public:
  CLASS_NON_COPYABLE_NON_MOVABLE(IntrusiveLinkedList);

  explicit IntrusiveLinkedList(Owner* owner) : owner_(owner) {}

  ~IntrusiveLinkedList() {
    if constexpr (Traits::list_type == IntrusiveListType::Owning) {
      auto to_delete = head_;

      while (to_delete) {
        const auto node = to_delete;

        to_delete = node->next_;

        // Make sure user can add his own logic to destroy function.
        static_cast<T*>(node)->destroy();
      }
    }
  }

  T* head() { return static_cast<T*>(head_); }
  T* tail() { return static_cast<T*>(tail_); }

  const T* head() const { return static_cast<const T*>(head_); }
  const T* tail() const { return static_cast<const T*>(tail_); }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  void insert_before(T* node, T* before) {
    const auto list_node = to_node(node);
    const auto before_node = to_node(before);

    own_node(list_node);

    if (before == nullptr) {
      const auto previous_head = head_;

      head_ = list_node;

      list_node->previous_ = nullptr;
      list_node->next_ = previous_head;

      if (previous_head) {
        verify(!previous_head->previous_, "invalid previous link");

        previous_head->previous_ = list_node;
      } else {
        verify(!tail_, "invalid tail node");

        tail_ = list_node;
      }
    } else {
      verify(before_node->owner_ == owner_, "before node is not owned by this list");

      list_node->next_ = before_node;
      list_node->previous_ = before_node->previous_;

      if (before_node->previous_) {
        before_node->previous_->next_ = list_node;
      } else {
        verify(before_node == head_, "list corruption");

        head_ = list_node;
      }

      before_node->previous_ = list_node;
    }
  }

  void insert_after(T* node, T* after) {
    const auto list_node = to_node(node);
    const auto after_node = to_node(after);

    own_node(list_node);

    if (after == nullptr) {
      const auto previous_tail = tail_;

      tail_ = list_node;

      list_node->previous_ = previous_tail;
      list_node->next_ = nullptr;

      if (previous_tail) {
        verify(!previous_tail->next_, "invalid next link");
        previous_tail->next_ = list_node;
      } else {
        verify(!head_, "invalid head node");

        head_ = list_node;
      }
    } else {
      verify(after_node->owner_ == owner_, "after node is not owned by this list");

      list_node->next_ = after_node->next_;
      list_node->previous_ = after_node;

      if (after_node->next_) {
        after_node->next_->previous_ = list_node;
      } else {
        verify(after_node == tail_, "list corruption");

        tail_ = list_node;
      }

      after_node->next_ = list_node;
    }
  }

  void unlink(T* node) {
    const auto unlink_node = to_node(node);

    verify(unlink_node->owner_ == owner_, "cannot unlink this node, it's not owned by us");

    if (unlink_node->previous_) {
      unlink_node->previous_->next_ = unlink_node->next_;
    } else {
      verify(unlink_node == head_, "list corruption");

      head_ = unlink_node->next_;
    }

    if (unlink_node->next_) {
      unlink_node->next_->previous_ = unlink_node->previous_;
    } else {
      verify(unlink_node == tail_, "list corruption");

      tail_ = unlink_node->previous_;
    }

    unlink_node->next_ = nullptr;
    unlink_node->previous_ = nullptr;
    unlink_node->owner_ = nullptr;

    size_--;

    Traits::on_node_removed(owner_, static_cast<T*>(node));
  }

  void push_front(T* insert_node) { insert_before(insert_node, nullptr); }
  void push_back(T* insert_node) { insert_after(insert_node, nullptr); }

  using iterator = IteratorInternal<T, Node, false>;
  using const_iterator = IteratorInternal<const T, const Node, false>;

  using reverse_iterator = IteratorInternal<T, Node, true>;
  using const_reverse_iterator = IteratorInternal<const T, const Node, true>;

  iterator begin() { return iterator(head_); }
  iterator end() { return iterator(nullptr); }

  const_iterator begin() const { return const_iterator(head_); }
  const_iterator end() const { return const_iterator(nullptr); }

  reverse_iterator rbegin() { return reverse_iterator(tail_); }
  reverse_iterator rend() { return reverse_iterator(nullptr); }

  const_reverse_iterator rbegin() const { return const_reverse_iterator(tail_); }
  const_reverse_iterator rend() const { return const_reverse_iterator(nullptr); }
};

}  // namespace base::ll