use linked_list::List;

fn main() {
    let mut list: List<i32> = List::new();

    println!("pushing 1, 2, 3");
    list.push(1);
    list.push(2);
    list.push(3);

    println!("peek: {:?}", list.peek());

    println!("doubling every element via iter_mut");
    for elem in list.iter_mut() {
        *elem *= 2;
    }

    print!("contents front-to-back via iter():");
    for elem in list.iter() {
        print!(" {elem}");
    }
    println!();

    print!("draining via into_iter():");
    for elem in list.into_iter() {
        print!(" {elem}");
    }
    println!();
}
