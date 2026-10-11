# Creates configs/connect.ip next to the built program (kept untouched if it already exists).
if(NOT EXISTS "${OUT}")
  file(WRITE "${OUT}" "# MINI max: server address (one line). Examples:\n#   https://my-server.example.com\n#   203.0.113.10:8080\n#   bore.pub:12345\n127.0.0.1:8080\n")
endif()
