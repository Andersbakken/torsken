#!/usr/bin/env node

const argv = require("minimist")(process.argv.slice(2));
const fs = require("fs");
const path = require("path");
const util = require('util');
const exec = util.promisify(require('child_process').exec);
const readFile = util.promisify(fs.readFile);
const nexline = require("nexline");
const prettyBytes = require("pretty-bytes");

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

class Allocation
{
    constructor(type, size, offset)
    {
        this.type = type;
        this.size = size;
        this.offset = offset;
        // console.log("got newed", arguments);
    }
};

class Snapshot
{
    constructor(time, bytes, allocationCount)
    {
        this.time = time;
        this.bytes = bytes;
        this.allocationCount = allocationCount;
    }

    toString()
    {
        return `${new Date(this.time).toISOString().substr(11, 8)} - ${prettyBytes(this.bytes)} - ${this.allocationCount} allocs`;
    }
}

async function processData(path)
{
    const data = await readFile(path, "utf8");
    let offset = 0;
    let last = 0;
    let count = 0;
    const current = new Map();
    const snapshots = [];
    let lastSnapshot = undefined;
    let bytesAllocated = 0;
    while (true) {
        const idx = data.indexOf("\n", last);
        if (idx === -1)
            break;
        // let addrsStart = data.indexOf(",", data.indexOf(",", last + 11) + 1);
        // console.log("got addrsStart", last, idx, addrsStart, data.indexOf(",", last + 11));
        const split = data.substring(last + 2, idx).split(",");
        // const line = data.substring(last, idx);
        const old = last;
        last = idx + 1;
        if (split.length < 3) {
            // console.log("bad line", data.substring(old, idx));
            // process.exit(0);
            continue;
        }
        const time = parseInt(split[2]);

        if (lastSnapshot === undefined) {
            if (time >= 1000) {
                // discard allocations that happened before sStarted was set
                continue;
            }
            lastSnapshot = time;
        } else if (time - lastSnapshot >= 500) {
            snapshots.push(new Snapshot(lastSnapshot, bytesAllocated, current.size));
            // console.log("bytesAllocated", bytesAllocated);
            lastSnapshot = time;
        }

        // console.log(split, count);
        const ptr = split[0];
        if (data[last] === 'f') {
            // console.log("got free", data[last]);
            let c = current.get(ptr);
            if (c) {
                bytesAllocated -= c.size;
                // console.log("fuck3", bytesAllocated);
                current.delete(ptr);
            } else {
                // console.log("Can't find free", split);
            }
        } else {
            const alloc = new Allocation(data[last], parseInt(split[1]), parseInt(split[2]));
            // console.log("fuck", bytesAllocated);
            bytesAllocated += alloc.size;
            // if (isNaN(bytesAllocated)) {
            //     console.log("shit", bytesAllocated, alloc.size, line);
            //     process.exit();
            // }
            // console.log("bytesAllocated", data[last], bytesAllocated, alloc.size,
            //             line);
            // console.log("fuck2", bytesAllocated);
            // console.log(typeof alloc.size, alloc, split[1], split);
            current.set(ptr, alloc);
        }

        // if (++count > 100)
        //     break;
    }

    console.log("snapshots", snapshots.map(x => x.toString()));

    // console.log("got data", data.length, count);
}

async function main()
{
    await ldd(argv.exec);
    // console.log("got libs", libs);
    await processData(argv.file);
}
main();
