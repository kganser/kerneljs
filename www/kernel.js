var kernel; if (!kernel) kernel = (function(modules, clients) {
  
  var dispatch = function(client) {
    for (var i = client ? clients.push(client)-1 : 0; i < clients.length; i++) {
      var dependencies = [], count = 0;
      for (var module in clients[i].dependencies) {
        if (clients[i].dependencies.hasOwnProperty(module) && ++count) {
          if (!modules[module]) { dependencies = false; break; }
          var versions = clients[i].dependencies[module];
          for (var j = 0; j < versions.length && dependencies.length != count; j++) {
            var major = versions[j][0], minors = versions[j][1];
            if (!modules[module][major]) continue;
            major = modules[module][major];
            for (var k = 0; k < minors.length && dependencies.length != count; k++) {
              if (k && typeof minors[k] == "number") {
                if (major[minors[k]]) dependencies.push({name: module, module: major[minors[k]]});
              } else {
                for (var minor = major.length-1; minor >= 0; minor--) {
                  if (major.hasOwnProperty(minor)) {
                    if (typeof minors[k] == "number" ? minor >= minors[k] : minor <= minors[k][1] && minor >= minors[k][0]) {
                      dependencies.push({name: module, module: major[minor]});
                      break;
                    }
                  }
                }
              }
            }
          }
          if (dependencies.length != count) { dependencies = false; break; }
        }
      }
      if (dependencies) {
        var dependency, m = {};
        while (dependency = dependencies.shift()) {
          if (!dependency.module.hasOwnProperty("instance")) dependency.module.instance = dependency.module.init();
          m[dependency.name] = dependency.module.instance;
        }
        clients.splice(i--, 1)[0].callback(m);
      }
    }
  };
  
  return {
    add: function(name, module, major, minor) {
      if (typeof major != "number") major = 0;
      if (typeof minor != "number") minor = 0;
      if (typeof module != "function") throw new Error("Invalid module");
      if (!modules.hasOwnProperty(name)) {
        if (name in modules) throw new Error("Invalid module name");
        modules[name] = [];
      }
      if (!modules[name][major]) modules[name][major] = [];
      if (!modules[name][major][minor]) {
        modules[name][major][minor] = {init: module};
        dispatch();
      }
      return this;
    },
    use: function(modules, callback) {
      var client = {dependencies: {}, callback: callback};
      for (var module in modules) {
        if (modules.hasOwnProperty(module)) {
          var dependency = client.dependencies[module] = [];
          if (modules[module] instanceof Array) {
            for (var i = 0; i < modules[module].length; i++) {
              if (typeof modules[module][i] == "number") {
                dependency[i] = [modules[module][i], [0]];
              } else if (modules[module][i] instanceof Array) {
                var major = modules[module][i][0];
                var minor = modules[module][i][1];
                dependency[i] = [typeof major == "number" ? major : 0, []];
                if (minor instanceof Array) {
                  for (var j = minor.length-1; j >= 0; j--) {
                    if (typeof minor[j] == "number") {
                      dependency[i][1].push(minor[j]);
                    } else if (minor[j] instanceof Array && minor[j].length) {
                      dependency[i][1].push(minor[j].length == 1 ? minor[j][0] : [minor[j][0], minor[j][1]]);
                    }
                  }
                }
                if (!dependency[i][1].length) {
                  dependency[i][1].push(typeof minor == "number" ? minor : 0);
                } else {
                  dependency[i][1].sort(function(a, b) {
                    return (typeof b == "number" ? b : b[1]) - (typeof a == "number" ? a : a[1]);
                  });
                }
              }
            }
          }
          if (!dependency.length) {
            dependency.push([typeof modules[module] == "number" ? modules[module] : 0, [0]]);
          } else {
            dependency.sort(function(a, b) { return b[0] - a[0]; });
          }
        }
      }
      dispatch(client);
      return this;
    }
  };
})({}, []);