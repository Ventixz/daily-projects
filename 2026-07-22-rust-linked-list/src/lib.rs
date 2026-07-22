//! A singly-linked stack, built by following the first chapter of
//! "Learning Rust with Too Many Linked Lists".

pub struct List<T> {
    head: Link<T>,
}

type Link<T> = Option<Box<Node<T>>>;

struct Node<T> {
    elem: T,
    next: Link<T>,
}

impl<T> List<T> {
    pub fn new() -> Self {
        List { head: None }
    }

    pub fn push(&mut self, elem: T) {
        let new_node = Box::new(Node {
            elem,
            next: self.head.take(),
        });
        self.head = Some(new_node);
    }

    pub fn pop(&mut self) -> Option<T> {
        self.head.take().map(|node| {
            self.head = node.next;
            node.elem
        })
    }

    pub fn peek(&self) -> Option<&T> {
        self.head.as_ref().map(|node| &node.elem)
    }

    pub fn peek_mut(&mut self) -> Option<&mut T> {
        self.head.as_mut().map(|node| &mut node.elem)
    }

    pub fn is_empty(&self) -> bool {
        self.head.is_none()
    }

    // Deliberately named to match `IntoIterator::into_iter` rather than
    // implementing the trait itself; clippy flags the naming collision as a
    // style nit, which is fine for this exercise.
    #[allow(clippy::should_implement_trait)]
    pub fn into_iter(self) -> IntoIter<T> {
        IntoIter(self)
    }

    pub fn iter(&self) -> Iter<'_, T> {
        Iter {
            next: self.head.as_deref(),
        }
    }

    pub fn iter_mut(&mut self) -> IterMut<'_, T> {
        IterMut {
            next: self.head.as_deref_mut(),
        }
    }
}

impl<T> Default for List<T> {
    fn default() -> Self {
        Self::new()
    }
}

// Rust's derived Drop for List<T> would recurse through `next` one stack
// frame per node, so a long enough list blows the stack. This iterative
// version unlinks nodes from the front until the chain is empty, which
// keeps stack usage O(1) regardless of list length.
impl<T> Drop for List<T> {
    fn drop(&mut self) {
        let mut cur_link = self.head.take();
        while let Some(mut boxed_node) = cur_link {
            cur_link = boxed_node.next.take();
            // boxed_node goes out of scope here and gets dropped, but
            // its `next` is already None, so this drop doesn't recurse.
        }
    }
}

pub struct IntoIter<T>(List<T>);

impl<T> Iterator for IntoIter<T> {
    type Item = T;
    fn next(&mut self) -> Option<T> {
        self.0.pop()
    }
}

pub struct Iter<'a, T> {
    next: Option<&'a Node<T>>,
}

impl<'a, T> Iterator for Iter<'a, T> {
    type Item = &'a T;
    fn next(&mut self) -> Option<Self::Item> {
        self.next.map(|node| {
            self.next = node.next.as_deref();
            &node.elem
        })
    }
}

pub struct IterMut<'a, T> {
    next: Option<&'a mut Node<T>>,
}

impl<'a, T> Iterator for IterMut<'a, T> {
    type Item = &'a mut T;
    fn next(&mut self) -> Option<Self::Item> {
        self.next.take().map(|node| {
            self.next = node.next.as_deref_mut();
            &mut node.elem
        })
    }
}

#[cfg(test)]
mod test {
    use super::List;

    #[test]
    fn push_pop_peek() {
        let mut list = List::new();
        assert_eq!(list.pop(), None);

        list.push(1);
        list.push(2);
        list.push(3);
        assert_eq!(list.peek(), Some(&3));

        assert_eq!(list.pop(), Some(3));
        assert_eq!(list.pop(), Some(2));

        list.push(4);
        list.push(5);

        assert_eq!(list.pop(), Some(5));
        assert_eq!(list.pop(), Some(4));

        assert_eq!(list.pop(), Some(1));
        assert_eq!(list.pop(), None);
    }

    #[test]
    fn peek_mut_mutates_top() {
        let mut list = List::new();
        list.push(1);
        list.push(2);
        list.push(3);

        if let Some(top) = list.peek_mut() {
            *top = 42;
        }

        assert_eq!(list.peek(), Some(&42));
        assert_eq!(list.pop(), Some(42));
    }

    #[test]
    fn into_iter_consumes_in_lifo_order() {
        let mut list = List::new();
        list.push(1);
        list.push(2);
        list.push(3);

        let mut iter = list.into_iter();
        assert_eq!(iter.next(), Some(3));
        assert_eq!(iter.next(), Some(2));
        assert_eq!(iter.next(), Some(1));
        assert_eq!(iter.next(), None);
    }

    #[test]
    fn iter_borrows_without_consuming() {
        let mut list = List::new();
        list.push(1);
        list.push(2);
        list.push(3);

        let mut iter = list.iter();
        assert_eq!(iter.next(), Some(&3));
        assert_eq!(iter.next(), Some(&2));
        assert_eq!(iter.next(), Some(&1));
        assert_eq!(iter.next(), None);

        // list is untouched: iter only borrowed it.
        assert_eq!(list.peek(), Some(&3));
    }

    #[test]
    fn iter_mut_mutates_each_element() {
        let mut list = List::new();
        list.push(1);
        list.push(2);
        list.push(3);

        for elem in list.iter_mut() {
            *elem *= 10;
        }

        let mut iter = list.iter();
        assert_eq!(iter.next(), Some(&30));
        assert_eq!(iter.next(), Some(&20));
        assert_eq!(iter.next(), Some(&10));
    }

    #[test]
    fn is_empty_tracks_state() {
        let mut list = List::new();
        assert!(list.is_empty());
        list.push(1);
        assert!(!list.is_empty());
        list.pop();
        assert!(list.is_empty());
    }

    #[test]
    fn long_list_drops_without_stack_overflow() {
        let mut list = List::new();
        for i in 0..200_000 {
            list.push(i);
        }
        // Dropping here exercises the iterative Drop impl. The naive
        // derived Drop would recurse 200,000 frames deep and overflow
        // the stack before this test process exits.
        drop(list);
    }
}
