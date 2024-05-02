#pragma once
#include <utility>
#include <vector>

#include <base/Error.hpp>

namespace base {

template <typename Key, typename Fn = void (*)()>
class StartupCallbacks {
  struct CallbackList {
    std::vector<Fn> callbacks;
    bool invoked = false;
  };

  static CallbackList& callback_list() {
    static CallbackList callback_list;
    verify(!callback_list.invoked, "startup callback list was already invoked");
    return callback_list;
  }

 public:
  template <typename... Args>
  static void invoke(Args&&... args) {
    auto& list = callback_list();

    list.invoked = true;

    for (const auto& cb : list.callbacks) {
      cb(std::forward<Args>(args)...);
    }

    list.callbacks.clear();
    list.callbacks.shrink_to_fit();
  }

  static void register_callback(Fn&& callback) {
    auto& list = callback_list();
    list.callbacks.push_back(std::forward<Fn>(callback));
  }

  class Registration {
   public:
    Registration(Fn&& callback) { register_callback(std::forward<Fn>(callback)); }
  };
};

}  // namespace base