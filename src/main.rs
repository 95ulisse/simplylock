mod options;

fn main() {
    let opt = options::parse();
    println!("{:?}", opt);
}
