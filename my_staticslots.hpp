#pragma once

#include <cassert>
#include <algorithm>  // std::for_each, std::move*
#include <array>      // std::array
#include <exception>  // std::out_of_range
#include <iterator>   // std::reverse_iterator, std::distance
#include <memory>     // std::uninitialized_*,
#include <utility>    // std::aligned_storage
#include <bitset>

template<typename T, std::size_t Capacity>
struct StaticSlots {

  // Value type equal to T
  using value_type = T;
  // std::size_t without including a header just for this name
  using size_type = std::size_t;
  // std::ptrdiff_t without including a header just for this name
  using difference_type = std::ptrdiff_t;
  // Reference type is a regular reference, not a proxy
  using reference = value_type&;
  using const_reference = const value_type&;
  // Pointer is a regular pointer
  using pointer = value_type*;
  using const_pointer = const value_type*;
  // The static capacity of the static_vector
  static const size_type static_capacity = Capacity;

  // Use a specific storage type to satisfy alignment requirements
  using storage_type =
    std::aligned_storage_t<sizeof(value_type), alignof(value_type)>;
  // The array providing the inline storage for the elements.
  std::array<storage_type, static_capacity> m_data = {};
  // Bits
  std::bitset<Capacity> m_used = {};

  StaticSlots() {}
  StaticSlots(StaticSlots& o) = delete;
  StaticSlots& operator=(const StaticSlots& other) = delete;
  StaticSlots(StaticSlots&& other) = delete;
  StaticSlots& operator=(StaticSlots&& other) = delete;

  struct iterator {
    StaticSlots<T, Capacity>& parent;
    size_t index;
    T* operator->() noexcept {
      return &parent.data(index);
    }
    T& operator*() noexcept {
      return parent.data(index);
    }
    iterator& operator++() noexcept {
      while (index != Capacity) {
        index++;
        if (parent.m_used[index]) {
          break;
        }
      }
      return *this;
    }
    bool operator!=(const iterator& other) const {
      return index != other.index;
    }
    iterator operator++(int) noexcept {
      auto old = *this;
      operator++();
      return old;
    }
  };

  storage_type* storage_data(size_t index) noexcept {
    return &m_data[index];
  }
  reference data(size_t index) noexcept {
    assert(m_used[index]);
    return *reinterpret_cast<pointer>(storage_data(index));
  }

  // Iterator is a regular pointer
  using const_iterator = iterator;
  // Reverse iterator is what the STL provides for reverse iterating pointers
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  auto begin() noexcept {
    return iterator{ *this, find_first_used() };
  }

  auto end() noexcept {
    return iterator{ *this, Capacity };
  }

  void clear() noexcept {
    for (size_t i = 0; i < m_used.size(); ++i) {
      if (m_used[i]) {
        data(i).~value_type();
      }
    }
    m_used.reset();
  }

  bool full() noexcept {
    return m_used.all();
  }

  size_t find_first_used() noexcept {
    for (size_t i = 0; i < m_used.size(); ++i) {
      if (m_used[i]) {
        return i;
      }
    }
    return Capacity;
  }

  size_t find_first_free() noexcept {
    for (size_t i = 0; i < m_used.size(); ++i) {
      if (!m_used[i]) {
        return i;
      }
    }
    return Capacity;
  }

  iterator erase(iterator pos) {
    pos->~value_type();
    m_used[pos.index] = false;
    return pos;
  }

  template<class... Args>
  pointer emplace_back(Args&&... args) noexcept {
    size_t index = find_first_free();
    if (index == Capacity) return nullptr;
    m_used[index] = true;
    return new (storage_data(index)) value_type(std::forward<Args>(args)...);
  }
};