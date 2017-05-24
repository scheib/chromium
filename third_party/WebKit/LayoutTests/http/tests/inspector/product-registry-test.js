
var initialize_ProductRegistry = function() {

InspectorTest.resetProductRegistry = function() {
  InspectorTest.addResult("Cleared ProductRegistryImpl");
  ProductRegistryImpl._productsByDomainHash.clear();
};

/**
 * @param {string} domainPattern
 * @param {string} productName
 * @param {number=} type
 */
InspectorTest.addProductRegistryEntry = function(domainPattern, productName, type) {
  InspectorTest.addResult("Adding entry: " + domainPattern);
  var wildCardPosition = domainPattern.indexOf('*');
  var prefix = '';
  if (wildCardPosition === -1) {
    // If not found '' means exact match.
  } else if (wildCardPosition === 0) {
    prefix = '*';
    console.assert(domainPattern.substr(1, 1) === '.', 'Domain pattern wildcard must be followed by \'.\'');
    domainPattern = domainPattern.substr(2);  // Should always be *.example.com (note dot, so 2 chars).
  } else {
    prefix = domainPattern.substr(0, wildCardPosition);
    console.assert(
        domainPattern.substr(wildCardPosition + 1, 1) === '.', 'Domain pattern wildcard must be followed by \'.\'');
    domainPattern = domainPattern.substr(wildCardPosition + 2);
  }
  console.assert(domainPattern.indexOf('*') === -1, 'Domain pattern may only have 1 wildcard.');

  var prefixes = {};
  prefixes[prefix] = {product: 0, type: type};

  ProductRegistryImpl.register(
      [productName], [{hash: ProductRegistryImpl._hashForDomain(domainPattern), prefixes: prefixes}]);
};

};
