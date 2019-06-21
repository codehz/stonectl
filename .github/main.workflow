workflow "do build" {
  on = "push"
  resolves = ["build"]
}

action "build" {
  uses = "codehz/alpine-builder/action@ed273290a31e8c8d927d5b2b51fb58bafb661848"
  args = "-DBUILD_STATIC_STONECTL=ON"
}
