#pragma once
#include <corio/concepts.hpp>
#include <experimental/coroutine>
#include <variant>
#include <iostream>

namespace cor3ntin::corio {

struct operation_cancelled : std::exception {
    virtual const char* what() const noexcept {
        return "operation cancelled";
    }
};


struct oneway_task {
    struct promise_type {
        std::experimental::suspend_never initial_suspend() {
            return {};
        }
        std::experimental::suspend_never final_suspend() {
            return {};
        }
        void unhandled_exception() {
            std::terminate();
        }
        oneway_task get_return_object() {
            return {};
        }
        void return_void() {}
    };
};

template <typename Sender, typename Value>
struct sender_awaiter {
private:
    struct internal_receiver {
        sender_awaiter* this_;

        template <typename... Values>
        void set_value(Values&&... value) {
            this_->m_data.template emplace<1>(std::forward<Values>(value)...);
            this_->m_continuation.resume();
        }
        template <typename Error>
        void set_error(Error&& error) {
            if constexpr(std::is_same_v<Error, std::exception_ptr>) {
                this_->m_data.template emplace<2>(std::move(error));
            } else {
                this_->m_data.template emplace<2>(std::make_exception_ptr(std::move(error)));
            }
            this_->m_continuation.resume();
        }

        void set_done() {
            this_->m_data.template emplace<0>(std::monostate{});
            this_->m_continuation.resume();
        }
    };


    using value_type = Value;
    using coro_handle = std::experimental::coroutine_handle<>;

    coro_handle m_continuation{};
    using operation_type = decltype(
        corio::execution::connect(std::declval<Sender>(), std::declval<internal_receiver>()));
    operation_type m_op;
    std::variant<std::monostate, details::non_void_t<value_type>, std::exception_ptr> m_data;


public:
    sender_awaiter(Sender sender) noexcept
        : m_op(corio::execution::connect(std::move(sender), internal_receiver{this})) {}
    sender_awaiter(sender_awaiter&& that) = default;
    ~sender_awaiter() {}


    static constexpr bool await_ready() noexcept {
        return false;
    }

    void await_suspend(coro_handle continuation) noexcept {
        m_continuation = continuation;
        corio::execution::start(m_op);
        return;
    }

    decltype(auto) await_resume() {
        switch(m_data.index()) {
            case 0: throw operation_cancelled{}; break;
            case 1: return std::get<1>(m_data); break;
            case 2: std::rethrow_exception(std::move(std::get<2>(m_data))); break;
        }
        return std::get<1>(m_data);
    }
};

template <cor3ntin::corio::execution::sender S>
auto operator co_await(S&& sender) {
    return cor3ntin::corio::sender_awaiter<S, void>(std::forward<S>(sender));
}
template <cor3ntin::corio::execution::typed_sender_single S>
auto operator co_await(S&& sender) {
    return cor3ntin::corio::sender_awaiter<S, cor3ntin::corio::execution::single_value_result_t<S>>(
        std::forward<S>(sender));
}

}  // namespace cor3ntin::corio