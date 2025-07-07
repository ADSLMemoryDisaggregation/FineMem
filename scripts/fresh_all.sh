#!/bin/bash

./stop_remote_client.sh 0 17 >/dev/null 2>&1

./stop_server.sh >/dev/null 2>&1

./start_server.sh >/dev/null 2>&1

./stop_server.sh >/dev/null 2>&1
