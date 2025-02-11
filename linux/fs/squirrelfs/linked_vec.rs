/* Vector type that gets around the maximum size limitation
 * for Rust vectors in the kernel.
 * This is tricky oof
 */

use core::cell::RefCell;
use kernel::prelude::*;
use kernel::sync::Arc;

struct LinkedVecNode<T: Sized + Copy> {
    vec: Vec<T>,
    next: Option<Arc<RefCell<LinkedVecNode<T>>>>,
}

impl<T: Sized + Copy> LinkedVecNode<T> {
    fn new() -> Self {
        Self {
            vec: Vec::new(),
            next: None,
        }
    }

    fn try_push(&mut self, new_val: &T) -> Result<()> {
        self.vec.try_push(*new_val)?;
        Ok(())
    }

    fn len(&self) -> usize {
        self.vec.len()
    }

    fn get(&self, i: usize) -> Option<&T> {
        self.vec.get(i)
    }
}

pub(crate) struct LinkedVec<T: Sized + Copy> {
    // Rc would be better here since this isn't shared between threads,
    // but it's not currently implemented in the kernel
    head: Option<Arc<RefCell<LinkedVecNode<T>>>>,
    tail: Option<Arc<RefCell<LinkedVecNode<T>>>>,
    len: usize,
}

impl<T: Sized + Copy> LinkedVec<T> {
    pub(crate) fn new() -> Self {
        Self {
            head: None,
            tail: None,
            len: 0,
        }
    }

    pub(crate) fn append(&mut self, new_val: &T) -> Result<()> {
        if let Some(tail) = &self.tail {
            let tail = Arc::clone(&tail);
            let mut tail = tail.borrow_mut();
            // try to append the new value to the tail.
            if tail.try_push(new_val).is_err() {
                // if it fails, we need to create a new node
                let mut new_node = LinkedVecNode::new();
                // if we can't push to the new node, then
                // we are actually out of memory
                new_node.try_push(new_val)?;

                // update the tail's next and the list's tail pointer
                let new_node_ref = Arc::try_new(RefCell::new(new_node))?;
                tail.next = Some(Arc::clone(&new_node_ref));
                self.tail = Some(Arc::clone(&new_node_ref));
            }
            self.len += 1;
            // otherwise, the push succeeded, so we're done
        } else {
            // list is empty, need to create a new node
            let mut new_node = LinkedVecNode::new();
            new_node.try_push(new_val)?;

            // update the tail's next and the list's tail pointer
            let new_node_ref = Arc::try_new(RefCell::new(new_node))?;
            // tail.next = Some(Arc::clone(new_node_ref));
            self.head = Some(Arc::clone(&new_node_ref));
            self.tail = Some(Arc::clone(&new_node_ref));
        }
        Ok(())
    }

    pub(crate) fn get(&self, i: usize) -> Option<T> {
        if i > self.len {
            return None;
        }

        let mut current_index = 0;
        let mut current_node = if let Some(head) = &self.head {
            Arc::clone(head)
        } else {
            return None;
        };

        while current_index <= i {
            let current_node_len = {
                let node = current_node.borrow();
                node.len()
            };

            if current_index <= i && current_index + current_node_len > i {
                let index = i - current_index;
                let node = current_node.borrow();
                return node.get(index).copied();
            } else {
                // node is in a later index
                current_index += current_node_len;
                let node = current_node.borrow();
                let next = &node.next;
                current_node = if let Some(next) = next {
                    Arc::clone(&next)
                } else {
                    return None;
                };
            }
        }

        // while current_node.is_some() {
        //     if let Some(node) = &current_node {
        //         let node = Arc::clone(node);
        //         let node = node.borrow();
        //         if current_index <= i && current_index + node.len() > i {
        //             // node is in this index
        //             let index = i - current_index;
        //             return node.get(index).copied();
        //         } else {
        //             // node is in a later index
        //             current_index += node.len();
        //             current_node = &node.next;
        //         }
        //     } else {
        //         unreachable!();
        //     }
        // }
        None
    }
}
