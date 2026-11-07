#!/usr/bin/env node
'use strict';
const { QuoteMachine, mulberry32, format } = require('./quotes');
const count = Number(process.argv[2] || 1);
const seed = process.argv[3];
const m = new QuoteMachine(undefined, seed === undefined ? Math.random : mulberry32(Number(seed)));
for (let i = 0; i < count; i++) console.log(format(m.next()));
