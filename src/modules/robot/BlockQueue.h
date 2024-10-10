#pragma once

#include <array>
#include "Block.h"

template<unsigned int length>
class __attribute__((packed)) BlockQueue {

    // friend classes
    friend class Planner;
    friend class Conveyor;

public:
    BlockQueue() {
        head_i = 0;
        tail_i = 0;
        isr_tail_i = tail_i;
    }

    /*
     * direct accessors
     */
    Block& head() { return ring[head_i]; }
    Block& tail() { return ring[tail_i]; }

    void push_front(Block& item) { // instead, prepare(head_ref()); produce_head();
        ring[head_i] = item;
        head_i = next(head_i);
    } __attribute__ ((warning("Not thread-safe if pop_back() is used in ISR context!")));

    Block& pop_back(void)  { // instead, consume(tail_ref()); consume_tail();
        Block& r = ring[tail_i];
        tail_i = next(tail_i);
        return r;
    } __attribute__ ((warning("Not thread-safe if head_ref() is used to prepare new items, or push_front() is used in ISR context!")));

    /*
     * pointer accessors
     */
    Block* head_ref() { return &ring[head_i]; }
    Block* tail_ref() { return &ring[tail_i]; }

    void produce_head(void) {
        while (is_full());
        head_i = next(head_i);
    }

    void consume_tail(void) {
        if (!is_empty())
            tail_i = next(tail_i);
    }

    /*
     * queue status
     */
    bool is_empty(void) const {
        //__disable_irq();
        bool r = (head_i == tail_i);
        //__enable_irq();

        return r;
    }

    bool is_full(void) const {
        //__disable_irq();
        bool r = (next(head_i) == tail_i);
        //__enable_irq();

        return r;
    }

protected:
    /*
     * these functions are protected as they should only be used internally
     * or in extremely specific circumstances
     */
    Block& item(unsigned int i) { return ring[i]; }
    Block* item_ref(unsigned int i) { return &ring[i]; }

    unsigned int next(unsigned int i) const {
        if (length == 0)
            return 0;

        if (++i >= length)
            return 0;

        return i;
    }

    unsigned int prev(unsigned int i) const {
        if (length == 0)
            return 0;

        if (i == 0)
            return (length - 1);
        else
            return (i - 1);
    }

    /*
     * buffer variables
     */
    volatile unsigned int head_i;
    volatile unsigned int tail_i;
    volatile unsigned int isr_tail_i;

private:
    std::array<Block, length> ring;
};