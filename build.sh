#!/usr/bin/env bash

gcc -g -o cweather wayweather.c src/*.c src/*.h -lcjson -lcurl
