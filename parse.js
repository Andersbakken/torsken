#!/usr/bin/env node

const argv = require("minimist")(process.argv.slice(2));
const fs = require("fs");
const path = require("path");
const util = require('util');
const exec = util.promisify(require('child_process').exec);
const nexline = require("nexline");

const Type = 0;
const Pointer = 1;
const Size = 2;
const Time = 3;
const Frames = 4;

try {
    if (!fs.lstatSync(argv.file).isFile())
        throw new Error(`${argv.file} is not a directory`);
} catch (err) {
    console.error(err.message);
    process.exit(1);
}

try {
    if (!fs.lstatSync(argv.exec).isFile())
        throw new Error(`${argv.exec} is not a file`);
} catch (err) {
    console.error(err.message);
    process.exit(1);
}

const lddExec = argv.ldd || "ldd";
const addr2lineExec = argv.addr2line || "addr2line";

const resolved = new Map();
const libs = new Set();

async function ldd(path)
{
    if (libs.has(path))
        return;
    libs.add(path);
    const data = await exec(`${lddExec} ${argv.exec}`);
    await Promise.all(data.stdout.split("\n").filter(x => { return x.indexOf(" => /") !== -1; }).map(line => {
        const p = line.split(" ")[2];
        // console.log("balls", p);
        return ldd(p);
    }));
}

async function resolveAddress(address)
{
    if (resolved.has(address))
        return resolved.get(address);
    const result = await exec(`${addr2lineExec} -e ${argv.exec} ${address}`);
    const data = result.stdout.trim();
    console.log("shit", typeof data, data);
    resolved.set(address, data);
    return data;
}

async function processData(path)
{
    const nl = nexline({
        input: fs.openSync(path, 'r'), // input can be file, stream, string and buffer
    });
     // nexline is iterable
    for await (const line of nl) {
        await Promise.all(line.split(",").map(resolveAddress));
    }
}

async function main()
{
    await ldd(argv.exec);
    console.log("got libs", libs);
    await processData(argv.file);
}
main();
