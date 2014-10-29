var kernel; if (!kernel) kernel = function(modules, clients) {
  
  var dispatch = function(client) {
    for (var i = client ? clients.push(client)-1 : 0; i < clients.length; i++) {
      client = clients[i];
      var needed = Object.keys(client.dependencies),
          available = [];
      if (!needed.some(function(module) { // if some is true, a needed module was not found
        return !modules[module] || !client.dependencies[module].some(function(version) { // if some is true, matching module was found; return false
          var major = modules[module][version[0]];
          return major && version[1].some(function(span, i, minors) {
            if (typeof span == 'number' && i < minors.length-1) // last number minor is treated as lower bound; others are exact
              return major.hasOwnProperty(span) && available.push({name: module, module: major[span]});
            for (var minor = major.length-1; minor >= 0; minor--)
              if (major.hasOwnProperty(minor) && (typeof span == 'number' ? minor >= span : minor <= span[1] && minor >= span[0]))
                return available.push({name: module, module: major[minor]});
          });
        });
      }) && available.length == needed.length) {
        var dependencies = {};
        available.forEach(function(dependency) {
          if (!dependency.module.hasOwnProperty('instance'))
            dependency.module.instance = dependency.module.init();
          dependencies[dependency.name] = dependency.module.instance;
        });
        clients.splice(i--, 1)[0].callback(dependencies);
      }
    }
  };
  
  return {
    add: function(name, module, dependencies, major, minor) {
      if (dependencies) return kernel.use(dependencies, function(o) {
        kernel.add(name, function() { return module(o); }, null, major, minor);
      });
      if (typeof major != 'number') major = 0;
      if (typeof minor != 'number') minor = 0;
      if (typeof module != 'function') throw new Error('Invalid module');
      if (!modules.hasOwnProperty(name)) {
        if (name in modules) throw new Error('Invalid module name "'+name+'"');
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
      
      // normalize module version format to (sorted desc): [[major, [minor, ...]], ...]
      // where `major` is number and `minor` is either number or number array [min, max]
      Object.keys(modules).forEach(function(module) {
        var versions = modules[module],
            normalized = client.dependencies[module] = [];
        if (Array.isArray(versions))
          versions.forEach(function(version, i) {
            if (typeof version == 'number') {
              normalized.push([version, [0]]);
            } else if (Array.isArray(version)) {
              var major = version[0],
                  minor = version[1];
              normalized.push([typeof major == 'number' ? major : 0, version = []]);
              if (Array.isArray(minor))
                minor.forEach(function(minor) {
                  if (typeof minor == 'number')
                    version.push(minor[j]);
                  else if (Array.isArray(minor) && minor.length)
                    version.push(minor.length == 1 ? minor[0] : [minor[0], minor[1]]);
                });
              if (!version.length)
                version.push(typeof minor == 'number' ? minor : 0);
              else
                version.sort(function(a, b) {
                  return (typeof b == 'number' ? b : b[1]) - (typeof a == 'number' ? a : a[1]);
                });
            }
          });
        if (!normalized.length)
          normalized.push([typeof versions == 'number' ? versions : 0, [0]]);
        else
          normalized.sort(function(a, b) { return b[0] - a[0]; });
      });
      dispatch(client);
      return this;
    }
  };
}({}, []);
