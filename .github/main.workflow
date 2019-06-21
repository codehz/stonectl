workflow "do build" {
  on = "push"
  resolves = ["build"]
}

action "build" {
  uses = "codehz/alpine-builder/action@206bb8ea6db32acefa8b493e4d63b867656e77db"
  args = "-DBUILD_STATIC_STONECTL=ON"
}
