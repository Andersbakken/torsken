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
const addr2lineExec = argv.addr2line || "eu-addr2line";

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

async function resolveAddresses(addresses, index)
{
    const filtered = addresses.filter(addr => !resolved.has(addr));
    if (filtered.length) {
        console.log(index, "=>", filtered.length, addresses.length);
        const result = await exec(`${addr2lineExec} -e ${argv.exec} -a ${filtered.join(" ")}`);
        const data = result.stdout.trim().split("\n");
        // console.log("shit", typeof data, data, addresses, filtered);
        for (let i=0; i<data.length; i += 2) {
            const addr = filtered[i / 2];
            resolved.set(addr, data[i + 1]);
            // console.log("set", addr, data[i + 1]);
        }
        // process.exit();
        // resolved.set(address, data);
        // return data;
    }
}

async function processData(path)
{
    const nl = nexline({
        input: fs.openSync(path, 'r'), // input can be file, stream, string and buffer
    });
    // nexline is iterable
    var idx = 0;
    for await (const line of nl) {
        // await resolveAddresses(line.split(",").slice(Frames), idx++);
        // if (++idx % 100 === 0)
            // console.log(++idx);
    }
}

async function main()
{
    await ldd(argv.exec);
    // console.log("got libs", libs);
    await processData(argv.file);
}
main();
