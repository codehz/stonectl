workflow "do build" {
  on = "push"
  resolves = ["codehz/alpine-builder/action@master"]
}

action "codehz/alpine-builder/action@master" {
  uses = "codehz/alpine-builder/action@master"
  args = "-DBUILD_STATIC_STONECTL=ON"
}
