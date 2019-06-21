workflow "do build" {
  on = "repository_dispatch"
  resolves = ["codehz/alpine-builder/action@master"]
}

action "codehz/alpine-builder/action@master" {
  uses = "codehz/alpine-builder/action@master"
  args = "-DBUILD_STATIC_STONECTL=ON"
}
