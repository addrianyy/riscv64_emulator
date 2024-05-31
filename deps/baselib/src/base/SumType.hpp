#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>

#include "Overload.hpp"

namespace base {

namespace detail::sum_type {

template <typename T>
struct SizeCalculator {
  constexpr static std::size_t N = sizeof(T);
};
template <typename T>
struct AlignmentCalculator {
  constexpr static std::size_t N = alignof(T);
};

template <template <typename> typename C, typename T>
constexpr std::size_t type_max() {
  return C<T>::N;
}
template <template <typename> typename C, typename T0, typename T1, typename... Args>
constexpr std::size_t type_max() {
  return std::max(C<T0>::N, type_max<C, T1, Args...>());
}

template <typename First, typename... Variants>
struct VariantIDType {
  using T = std::remove_cvref_t<decltype(First::variant_id)>;
};

template <bool Constant, typename Fn, typename T, typename... Args>
constexpr auto visit(auto variant_id, auto* storage_ptr, Fn&& fn) {
  if (variant_id == T::variant_id) {
    using QualifiedT = std::conditional_t<Constant, const T, T>;

    if constexpr (std::is_invocable_v<Fn, QualifiedT&>) {
      return fn(*reinterpret_cast<QualifiedT*>(storage_ptr));
    } else if constexpr (std::is_invocable_v<Fn, QualifiedT*>) {
      return fn(reinterpret_cast<QualifiedT*>(storage_ptr));
    } else {
      static_assert(std::is_invocable_v<Fn>,
                    "visit is not exhaustive but doesn't have any default case");
      return fn();
    }
  } else {
    if constexpr (sizeof...(Args) == 0) {
      // Unreachable code.
      // TODO: Replace with `std::unreachable()` in C++23.
#if defined(__GNUC__)
      __builtin_unreachable();
#elif defined(_MSC_VER)
      __assume(false);
#endif
    } else {
      return visit<Constant, Fn, Args...>(variant_id, storage_ptr, std::forward<Fn>(fn));
    }
  }
}

template <typename... Variants>
consteval bool are_variants_unique() {
  const std::array<typename VariantIDType<Variants...>::T, sizeof...(Variants)> variants{
    Variants::variant_id...};

  for (size_t i = 0; i < variants.size(); ++i) {
    const auto variant = variants[i];

    for (size_t j = 0; j < variants.size(); ++j) {
      if (i != j && variants[j] == variant) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace detail::sum_type

template <typename... Variants>
class SumType {
 public:
  using VariantIDType = detail::sum_type::VariantIDType<Variants...>::T;

 private:
  using Storage = std::aligned_storage_t<
    detail::sum_type::type_max<detail::sum_type::SizeCalculator, Variants...>(),
    detail::sum_type::type_max<detail::sum_type::AlignmentCalculator, Variants...>()>;

  static_assert(detail::sum_type::are_variants_unique<Variants...>(),
                "SumType variants are not unique");

  VariantIDType variant_id;
  Storage storage;

  template <typename T>
  static constexpr void check_variant_type() {
    static_assert((std::is_same_v<T, Variants> || ...),
                  "T is not one of the types specified as variant");
  }

  constexpr void destroy() {
    visit([]<typename V>(V* value) { value->~V(); });
  }

 public:
  constexpr SumType(const SumType& other) {
    other.visit([this]<typename V>(V& value) {
      variant_id = V::variant_id;
      new (&storage) V(value);
    });
  }

  constexpr SumType(SumType&& other) noexcept {
    other.visit([this]<typename V>(V& value) {
      variant_id = V::variant_id;
      new (&storage) V(std::move(value));
    });
  }

  constexpr SumType& operator=(const SumType& other) {
    if (this != &other) {
      destroy();

      other.visit([this]<typename V>(V& value) {
        variant_id = V::variant_id;
        new (&storage) V(value);
      });
    }

    return *this;
  }

  constexpr SumType& operator=(SumType&& other) noexcept {
    if (this != &other) {
      destroy();

      other.visit([this]<typename V>(V& value) {
        variant_id = V::variant_id;
        new (&storage) V(std::move(value));
      });
    }

    return *this;
  }

  template <typename T>
  constexpr SumType(T&& value) {
    check_variant_type<T>();

    variant_id = T::variant_id;
    new (&storage) T(std::forward<T>(value));
  }

  template <typename T>
  constexpr SumType& operator=(T&& value) {
    check_variant_type<T>();

    destroy();

    variant_id = T::variant_id;
    new (&storage) T(std::forward<T>(value));
  }

  ~SumType() { destroy(); }

  VariantIDType id() const { return variant_id; }

  template <typename Fn>
  constexpr auto visit(Fn&& fn) {
    return detail::sum_type::visit<false, Fn, Variants...>(variant_id, &storage,
                                                           std::forward<Fn>(fn));
  }

  template <typename Fn>
  constexpr auto visit(Fn&& fn) const {
    return detail::sum_type::visit<true, Fn, Variants...>(variant_id, &storage,
                                                          std::forward<Fn>(fn));
  }

  template <typename T>
  T* as() {
    check_variant_type<T>();

    if (variant_id == T::variant_id) {
      return reinterpret_cast<T*>(&storage);
    } else {
      return nullptr;
    }
  }

  template <typename T>
  const T* as() const {
    check_variant_type<T>();

    if (variant_id == T::variant_id) {
      return reinterpret_cast<const T*>(&storage);
    } else {
      return nullptr;
    }
  }

  template <typename T>
  bool is() const {
    return as<T>() != nullptr;
  }
};

}  // namespace base